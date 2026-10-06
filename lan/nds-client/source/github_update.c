/* Direct, public GitHub HTTPS updater. Hub credentials never enter requests. */
#include "github_update.h"
#include "mini_core.h"
#include "net.h"
#include "bearssl.h"
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#ifdef __NDS__
#include <nds.h>
#include <sys/ioctl.h>
#include "debug_log.h"
#else
#include <arpa/inet.h>
#define closesocket close
#define debug_log(...) ((void)0)
#endif

#define UPDATE_LIMIT (1024u*1024u)
#define MANIFEST_URL "https://raw.githubusercontent.com/hugoalves6/OpenHome-LAN/lan-live-dsi/lan/nds-client/update.json"
#ifndef GITHUB_SEED_PATH
#define GITHUB_SEED_PATH "/openhome-updater.seed"
#endif
extern const br_x509_trust_anchor github_trust_anchors[];
extern const size_t github_trust_anchors_count;
static br_ssl_client_context client;
static br_x509_minimal_context verifier;
static br_sslio_context stream;
static unsigned char tls_buffer[BR_SSL_BUFSIZE_BIDI];
static void (*progress)(const char *,size_t,size_t);
static time_t deadline;
static int transport_failed;
#ifdef GITHUB_UPDATE_TEST
static int test_reject_certificates;
static const char *test_server_name;
#endif

void github_update_progress(void (*callback)(const char *,size_t,size_t)){progress=callback;}
static int fail(const char *message){snprintf(net_error,sizeof(net_error),"%s",message);return -1;}
static int store_seed(const unsigned char seed[32]){
    FILE *f=fopen(GITHUB_SEED_PATH ".new","wb");if(!f)return -1;
    int result=fwrite(seed,1,32,f)==32&&fflush(f)==0?0:-1;
    if(fclose(f))result=-1;
    /* libfat rename cannot overwrite a destination. A failed rotation closes
       the updater; the trusted hub can provision a new seed on the next login. */
    if(!result){remove(GITHUB_SEED_PATH);if(rename(GITHUB_SEED_PATH ".new",GITHUB_SEED_PATH))result=-1;}
    return result;
}
int github_update_ready(void){
    unsigned char seed[33];FILE *f=fopen(GITHUB_SEED_PATH,"rb");if(!f)return 0;
    size_t n=fread(seed,1,sizeof(seed),f);fclose(f);memset(seed,0,sizeof(seed));return n==32;
}
void github_update_seed(const char *hub_session){
    /* One-time bootstrap from the existing trusted LAN login's 128 random bits.
       Persisted and domain-separated thereafter; no time/rand()-only TLS keys. */
    if(github_update_ready()||strlen(hub_session)!=32)return;
    unsigned char seed[32];br_sha256_context hash;
    br_sha256_init(&hash);br_sha256_update(&hash,"OpenHome TLS bootstrap v1",25);
    br_sha256_update(&hash,hub_session,32);br_sha256_out(&hash,seed);
    if(store_seed(seed))debug_log("Updater seed could not be saved");
    memset(seed,0,sizeof(seed));
}
static int seed_tls(void){
    unsigned char seed[32],next[32],entropy[32];
    FILE *f=fopen(GITHUB_SEED_PATH,"rb");if(!f)return fail("Connect to hub once to set up updater.");
    size_t n=fread(seed,1,32,f);int extra=fgetc(f);fclose(f);
    if(n!=32||extra!=EOF)return fail("Updater seed invalid. Setup needed.");
    br_sha256_context hash;
    br_sha256_init(&hash);br_sha256_update(&hash,seed,32);br_sha256_update(&hash,"next",4);br_sha256_out(&hash,next);
    br_sha256_init(&hash);br_sha256_update(&hash,seed,32);br_sha256_update(&hash,"handshake",9);br_sha256_out(&hash,entropy);
    int result=store_seed(next);
    if(!result)br_ssl_engine_inject_entropy(&client.eng,entropy,sizeof(entropy));
    memset(seed,0,sizeof(seed));memset(next,0,sizeof(next));memset(entropy,0,sizeof(entropy));
    return result?fail("Cannot save updater state."):0;
}
static int cancelled(void){
    if(time(NULL)>deadline)return 1;
#ifdef __NDS__
    scanKeys();if(keysHeld()&KEY_B)return 1;
#endif
    return 0;
}
static int socket_io(void *ctx,unsigned char *buffer,size_t size,int sending){
    int sock=*(int*)ctx;
#ifdef __NDS__
    for(int frames=0;frames<900;frames++){
#else
    for(int attempts=0;attempts<2;attempts++){
#endif
        if(cancelled()){transport_failed=1;return -1;}
        int n=sending?send(sock,buffer,size,0):recv(sock,buffer,size,0);
        if(n>0)return n;
        if(n==0)break;
        if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINPROGRESS&&errno!=EINTR)break;
#ifdef __NDS__
        swiWaitForVBlank();
#endif
    }
    transport_failed=1;return -1;
}
static int tls_read(void *ctx,unsigned char *buffer,size_t n){return socket_io(ctx,buffer,n,0);}
static int tls_write(void *ctx,const unsigned char *buffer,size_t n){return socket_io(ctx,(unsigned char*)buffer,n,1);}

static int parse_url(const char *url,char host[96],char path[2048]){
    if(strncmp(url,"https://",8))return -1;
    const char *slash=strchr(url+8,'/');if(!slash)return -1;
    size_t n=(size_t)(slash-(url+8));if(!n||n>=96||strlen(slash)>=2048)return -1;
    memcpy(host,url+8,n);host[n]=0;strcpy(path,slash);
    for(const char *p=url;*p;p++)if((unsigned char)*p<=32||(unsigned char)*p>=127||*p=='#'||*p=='\\')return -1;
    if(!strcmp(host,"raw.githubusercontent.com"))
        return strcmp(url,MANIFEST_URL)?-1:0;
    if(!strcmp(host,"github.com"))
        return strncmp(path,"/hugoalves6/OpenHome-LAN/releases/download/",43)?-1:0;
    return strcmp(host,"release-assets.githubusercontent.com")&&strcmp(host,"objects.githubusercontent.com")?-1:0;
}
static int open_tls(const char *host,int *sock){
    if(progress)progress("Verifying GitHub HTTPS",0,0);
    /* DS Wi-Fi can need several minutes for a complete binary; inactivity
       remains bounded to 15 seconds independently of this total deadline. */
    deadline=time(NULL)+600;transport_failed=0;
    struct hostent *entry=gethostbyname(host);
    if(!entry||entry->h_addrtype!=AF_INET||entry->h_length!=4)return fail("GitHub DNS failed. Check internet.");
    struct sockaddr_in addr;memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET;addr.sin_port=htons(443);memcpy(&addr.sin_addr,entry->h_addr_list[0],4);
    *sock=socket(AF_INET,SOCK_STREAM,0);if(*sock<0)return -1;
#ifdef __NDS__
    int nonblocking=1;if(ioctl(*sock,FIONBIO,&nonblocking))return -1;
    int result=connect(*sock,(struct sockaddr*)&addr,sizeof(addr));
    if(result&&errno!=EINPROGRESS&&errno!=EWOULDBLOCK&&errno!=EAGAIN)return -1;
    if(result){
        int ready=0;
        for(int frames=0;frames<900&&!cancelled();frames++){
            struct sockaddr_in peer;socklen_t size=sizeof(peer);
            if(!getpeername(*sock,(struct sockaddr*)&peer,&size)){ready=1;break;}
            swiWaitForVBlank();
        }
        if(!ready)return fail("GitHub connection timed out.");
    }
#else
    struct timeval wait={15,0};setsockopt(*sock,SOL_SOCKET,SO_RCVTIMEO,&wait,sizeof(wait));setsockopt(*sock,SOL_SOCKET,SO_SNDTIMEO,&wait,sizeof(wait));
    if(connect(*sock,(struct sockaddr*)&addr,sizeof(addr))){snprintf(net_error,sizeof(net_error),"GitHub connect failed (%d).",errno);return -1;}
#endif
    br_ssl_client_init_full(&client,&verifier,github_trust_anchors,github_trust_anchors_count);
#ifdef GITHUB_UPDATE_TEST
    if(test_reject_certificates)br_ssl_client_init_full(&client,&verifier,github_trust_anchors,0);
#endif
    br_ssl_engine_set_versions(&client.eng,BR_TLS12,BR_TLS12);
    time_t now=time(NULL);
    if(now<1704067200)return fail("Set the console date for HTTPS.");
    br_x509_minimal_set_time(&verifier,(uint32_t)(now/86400)+719528,(uint32_t)(now%86400));
    br_ssl_engine_set_buffer(&client.eng,tls_buffer,sizeof(tls_buffer),1);
    if(seed_tls())return -1;
#ifdef GITHUB_UPDATE_TEST
    if(test_server_name)host=test_server_name;
#endif
    if(!br_ssl_client_reset(&client,host,0))return -1;
    br_sslio_init(&stream,&client.eng,tls_read,sock,tls_write,sock);
    return 0;
}
static int line(char *out,size_t capacity){
    size_t used=0;
    while(used+1<capacity){
        unsigned char c;if(br_sslio_read_all(&stream,&c,1))return -1;
        out[used++]=(char)c;
        if(c=='\n'){if(used<2||out[used-2]!='\r')return -1;out[used-2]=0;return 0;}
    }
    return -1;
}
static int get(const char *url,char *text,size_t capacity,const char *filename){
    static char current[2304],host[96],path[2048],header[8192],location[2304],request[2304];
    static unsigned char buffer[4096];
    if(strlen(url)>=sizeof(current))return -1;
    strcpy(current,url);
    int result=-1,sock=-1;FILE *file=NULL;
    snprintf(net_error,sizeof(net_error),"GitHub HTTPS failed. B cancels.");
    for(int redirects=0;redirects<5;redirects++){
        if(parse_url(current,host,path)){fail("Update URL is not allowed.");break;}
        if(open_tls(host,&sock))break;
        int n=snprintf(request,sizeof(request),"GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: OpenHome-DS\r\nAccept: application/octet-stream\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n",path,host);
        if(n<0||n>=(int)sizeof(request)||br_sslio_write_all(&stream,request,n)||br_sslio_flush(&stream))break;
        if(line(header,sizeof(header)))break;
        int status;if(sscanf(header,"HTTP/%*s %d",&status)!=1)break;
#ifdef GITHUB_UPDATE_TEST
        fprintf(stderr,"HTTPS host=%s status=%d\n",host,status);
#endif
        size_t length=0,headers=0;int have_length=0,chunked=0,bad=0;location[0]=0;
        while(1){
            if(line(header,sizeof(header))){bad=1;break;}
            headers+=strlen(header)+2;if(headers>32768){bad=1;break;}
            if(!*header)break;
            char *colon=strchr(header,':');if(!colon){bad=1;break;}*colon++=0;while(*colon==' '||*colon=='\t')colon++;
            if(!strcasecmp(header,"Location"))snprintf(location,sizeof(location),"%s",colon);
            else if(!strcasecmp(header,"Content-Length")){
                char *end;unsigned long value=strtoul(colon,&end,10);
                if(*end||end==colon||have_length||value>UPDATE_LIMIT){bad=1;break;}
                length=value;have_length=1;
            }else if(!strcasecmp(header,"Transfer-Encoding")){
                if(strcasecmp(colon,"chunked")){bad=1;break;}chunked=1;
            }else if(!strcasecmp(header,"Content-Encoding")&&strcasecmp(colon,"identity")){bad=1;break;}
        }
#ifdef GITHUB_UPDATE_TEST
        fprintf(stderr,"HTTP headers bad=%d length=%lu chunked=%d redirect=%d\n",bad,(unsigned long)length,chunked,!!*location);
#endif
        if(bad)break;
        if(status==301||status==302||status==303||status==307||status==308){
            if(!*location)break;
            /* Never downgrade or forward headers/credentials to arbitrary hosts. */
            if(strlen(location)>=sizeof(current))break;
            strcpy(current,location);
            closesocket(sock);sock=-1;continue;
        }
        if(status!=200){snprintf(net_error,sizeof(net_error),"GitHub returned HTTP %d.",status);break;}
        if(chunked&&have_length)break;
        if(!chunked&&!have_length)break;
        if(filename){file=fopen(filename,"wb");if(!file){fail("Cannot create update file.");break;}}
        size_t total=0,left=length;
        while(1){
            if(cancelled()){bad=1;break;}
            if(chunked){
                if(line(header,80)){bad=1;break;}
                char *end;unsigned long size=strtoul(header,&end,16);
                if(end==header||(*end&&*end!=';')||size>UPDATE_LIMIT-total){bad=1;break;}
                left=size;
                if(!left){
                    do{if(line(header,sizeof(header))||(headers+=strlen(header)+2)>32768){bad=1;break;}}while(*header);
                    break;
                }
            }
            if(left>UPDATE_LIMIT-total||(!filename&&left>=capacity-total)){bad=1;break;}
            while(left){
                size_t amount=left<sizeof(buffer)?left:sizeof(buffer);
                if(br_sslio_read_all(&stream,buffer,amount)){bad=1;break;}
                if(filename){if(fwrite(buffer,1,amount,file)!=amount){bad=1;break;}}
                else memcpy(text+total,buffer,amount);
                total+=amount;left-=amount;
                if(progress&&(total%16384==0||!left))progress("Downloading from GitHub",total,have_length?length:0);
            }
            if(bad||!chunked)break;
            unsigned char crlf[2];if(br_sslio_read_all(&stream,crlf,2)||memcmp(crlf,"\r\n",2)){bad=1;break;}
        }
        if(!bad&&total){
            if(file){if(!fflush(file))result=0;if(fclose(file))result=-1;file=NULL;}
            else {text[total]=0;result=0;}
        }
        break;
    }
    if(file)fclose(file);
    if(sock>=0)closesocket(sock);
    if(result){
        if(filename)remove(filename);
        debug_log("GitHub update failed TLS=%d transport=%d",br_ssl_engine_last_error(&client.eng),transport_failed);
    }
    return result;
}
int github_update_info(char *out,size_t cap){return get(MANIFEST_URL,out,cap,NULL);}
int github_update_download(const char *url,const char *temporary,const char *sha256){
    char hash[65];if(get(url,NULL,0,temporary))return -1;
    if(mini_sha_file(temporary,hash)||strcmp(hash,sha256)){remove(temporary);return fail("Update checksum mismatch.");}
    return 0;
}
int github_update_newer(const char *available,const char *installed){
    unsigned a,b,c,x,y,z;char tail;
    if(sscanf(available,"%u.%u.%u%c",&a,&b,&c,&tail)!=3||sscanf(installed,"%u.%u.%u%c",&x,&y,&z,&tail)!=3)return 0;
    if(a>999||b>999||c>999)return 0;
    return a!=x?a>x:b!=y?b>y:c>z;
}
