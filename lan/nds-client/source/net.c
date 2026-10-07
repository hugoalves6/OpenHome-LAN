#include "net.h"
#include "mini_core.h"
#include "mini_socket.h"
#ifdef __NDS__
#include "debug_log.h"
#else
#define debug_log(...) ((void)0)
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <errno.h>
#ifdef __NDS__
#include <nds.h>
#include <dswifi9.h>
#include <sys/ioctl.h>
/* dswifi socket timeouts are not relied upon: poll with an explicit limit. */
static int socket_io(int sock, void *data, size_t size, int sending) {
    for(int frames=0;frames<900;frames++) {
        int n=sending?send(sock,data,size,0):recv(sock,data,size,0);
        if(n>=0){if(!n)debug_log("TCP %s ended before response complete",sending?"send":"receive");return n;}
        if(errno!=EAGAIN && errno!=EWOULDBLOCK && errno!=EINPROGRESS){debug_log("TCP %s failed errno=%d wifi=%d",sending?"send":"receive",errno,Wifi_AssocStatus());return -1;}
        swiWaitForVBlank();scanKeys();if(keysHeld()&KEY_B){debug_log("TCP wait cancelled by B");return -1;}
    }
    debug_log("TCP %s timed out wifi=%d",sending?"send":"receive",Wifi_AssocStatus());return -1;
}
#define SOCKET_SEND(s,p,n) socket_io(s,(void*)(p),n,1)
#define SOCKET_RECV(s,p,n) socket_io(s,p,n,0)
#else
#define SOCKET_SEND(s,p,n) send(s,p,n,0)
#define SOCKET_RECV(s,p,n) recv(s,p,n,0)
#endif
#ifndef __NDS__
#include <arpa/inet.h>
#endif

char hub_host[64] = "192.168.1.125", hub_password[128] = "", hub_token[128] = "";
int hub_port = 8321;
char net_error[96]="Network request failed";
static size_t last_response_size;
static void (*progress_callback)(const char *,size_t,size_t);
void net_set_progress(void (*callback)(const char *,size_t,size_t)) { progress_callback=callback; }

static int send_all(int socket, const void *data, size_t size) {
    const char *p=data;
    while (size) { int n=SOCKET_SEND(socket,p,size); if (n<=0) return -1; p+=n; size-=n; }
    return 0;
}
static int connect_hub(void) {
    unsigned a,b,c,d; char extra;
    if (sscanf(hub_host,"%u.%u.%u.%u%c",&a,&b,&c,&d,&extra)!=4 || a!=192 || b!=168 || c!=1 || d>254 || d==0) return -1;
    int sock=socket(AF_INET,SOCK_STREAM,0); if (sock<0){debug_log("TCP socket allocation failed errno=%d",errno);return -1;}
    struct sockaddr_in addr; memset(&addr,0,sizeof(addr));
    addr.sin_family=AF_INET; addr.sin_port=htons(hub_port);
    addr.sin_addr.s_addr=inet_addr(hub_host);
    struct timeval timeout={30,0};
    setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
#ifdef __NDS__
    int nonblocking=1;
    if(ioctl(sock,FIONBIO,&nonblocking)){mini_socket_close(sock);return -1;}
    int result=connect(sock,(struct sockaddr*)&addr,sizeof(addr));
    if(result && errno!=EINPROGRESS && errno!=EWOULDBLOCK && errno!=EAGAIN){debug_log("TCP connect failed errno=%d wifi=%d",errno,Wifi_AssocStatus());mini_socket_close(sock);return -1;}
    if(result){
        int ready=0;
        for(int frames=0;frames<900;frames++){
            struct sockaddr_in peer;socklen_t size=sizeof(peer);
            if(!getpeername(sock,(struct sockaddr*)&peer,&size)){ready=1;break;}
            swiWaitForVBlank();scanKeys();if(keysHeld()&KEY_B)break;
        }
        if(!ready){debug_log("TCP connect wait ended wifi=%d",Wifi_AssocStatus());mini_socket_close(sock);return -1;}
    }
#else
    if (connect(sock,(struct sockaddr*)&addr,sizeof(addr))) { mini_socket_close(sock); return -1; }
#endif
    return sock;
}
static int encode(const char *path,char *out,size_t cap) {
    const char *hex="0123456789ABCDEF"; size_t used=0;
    while (*path) {
        unsigned char c=*path++;
        if (used+4>=cap) return -1;
        if ((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='/') out[used++]=c;
        else { out[used++]='%'; out[used++]=hex[c>>4]; out[used++]=hex[c&15]; }
    }
    out[used]=0; return 0;
}
static int begin(const char *method,const char *path,size_t size,const char *extra) {
    debug_log("HTTP begin %s %s bytes=%lu",method,path,(unsigned long)size);
    int sock=connect_hub(); if (sock<0) return -1;
    char headers[2048];
    int n=snprintf(headers,sizeof(headers),"%s %s HTTP/1.0\r\nHost: %s:%d\r\nX-Auth: %s\r\nContent-Length: %lu\r\nConnection: close\r\n%s\r\n",method,path,hub_host,hub_port,hub_token,(unsigned long)size,extra?extra:"");
    if (n<0 || n>=(int)sizeof(headers) || send_all(sock,headers,n)) { mini_socket_close(sock); return -1; }
    return sock;
}
static int response(int sock,size_t *length) {
    static char header[4096]; int used=0;
    while (used<(int)sizeof(header)-1) {
        if (SOCKET_RECV(sock,header+used,1)!=1) return -1;
        used++; header[used]=0;
        if (used>=4 && !memcmp(header+used-4,"\r\n\r\n",4)) break;
    }
    if (used==(int)sizeof(header)-1) return -1;
    int status=0; if (sscanf(header,"HTTP/%*s %d",&status)!=1) return -1;
    char *size=strstr(header,"Content-Length:");
    if (!size) return -1;
    char *end; unsigned long parsed=strtoul(size+15,&end,10);
    if (end==size+15 || parsed>1024*1024) return -1;
    *length=parsed; return status;
}
static int read_all(int sock,void *buffer,size_t size) {
    char *p=buffer;
    while (size) { int n=SOCKET_RECV(sock,p,size); if(n<=0)return -1; p+=n; size-=n; }
    return 0;
}
int net_last_status;
static int text_request(const char *method,const char *path,const char *body,char *out,size_t cap) {
    net_last_status=0;
    snprintf(net_error,sizeof(net_error),"Network wait failed. B cancels.");
    int sock=begin(method,path,body?strlen(body):0,NULL); if(sock<0)return -1;
    int result=-1; size_t length;
    if (body && send_all(sock,body,strlen(body))) goto done;
    int status=response(sock,&length);net_last_status=status;
    if (status<0 || length>=cap || read_all(sock,out,length)) goto done;
    out[length]=0;
    if(status!=200){debug_log("HTTP response status=%d",status);mini_json_string(out,"error",net_error,sizeof(net_error));goto done;}
    last_response_size=length;result=0;
done: mini_socket_close(sock); return result;
}
int net_login(void) {
    char body[256],result[256];
    for (const char *p=hub_password; *p; p++) if (*p=='"'||*p=='\\'||(unsigned char)*p<32) return -1;
    snprintf(body,sizeof(body),"{\"password\":\"%s\"}",hub_password);
    if(text_request("POST","/ohnx/login",body,result,sizeof(result)))return -1;
    return mini_json_string(result,"token",hub_token,sizeof(hub_token));
}
int net_live_pulse(const char *body){
    char result[256];return text_request("POST","/ohnx/live/pulse",body,result,sizeof(result));
}
int net_listing(char *out,size_t cap) {
    return text_request("GET","/ohnx/list?path=roms/nds",NULL,out,cap);
}
int net_update_info(char *out,size_t cap) {
    return text_request("GET","/ohnx/raw?path=updates/openhome-mini.json",NULL,out,cap);
}
int net_box(const char *remote,int box,char *out,size_t cap) {
    char encoded[1024],path[1152];
    if(encode(remote,encoded,sizeof(encoded)))return -1;
    snprintf(path,sizeof(path),"/ohnx/nds/box?path=%s&box=%d",encoded,box);
    if(text_request("GET",path,NULL,out,cap))return -1;
    /* Exact binary contract: header and 30 fixed-size slots. */
    if(cap<7809 || last_response_size!=7808 || memcmp(out,"OHB1",4) || out[7]!=30 || (out[4]!=4&&out[4]!=5) || out[6]!=box){
        snprintf(net_error,sizeof(net_error),"Invalid box response");return -1;
    }
    return 0;
}
int net_swap(const char *remote,const char *sha,int sb,int ss,int tb,int ts) {
    char body[768],result[256];
    /* Configuration already restricts paths; avoid JSON string escapes here. */
    if(strchr(remote,'"')||strchr(remote,'\\'))return -1;
    snprintf(body,sizeof(body),"{\"path\":\"%s\",\"sha256\":\"%s\",\"source_box\":%d,\"source_slot\":%d,\"target_box\":%d,\"target_slot\":%d}",remote,sha,sb,ss,tb,ts);
    return text_request("POST","/ohnx/nds/swap",body,result,sizeof(result));
}
int net_save_moves(const char *remote,const char *sha,const DraftMove *moves,int count) {
    static char body[4096];char result[256];
    if(count<1||count>DRAFT_MAX_MOVES||strchr(remote,'"')||strchr(remote,'\\'))return -1;
    int used=snprintf(body,sizeof(body),"{\"path\":\"%s\",\"sha256\":\"%s\",\"moves\":[",remote,sha);
    for(int i=0;i<count;i++) {
        if(used<0 || used>=(int)sizeof(body)-64)return -1;
        used+=snprintf(body+used,sizeof(body)-used,"%s[%u,%u,%u,%u]",i?",":"",moves[i].source_box,moves[i].source_slot,moves[i].target_box,moves[i].target_slot);
    }
    if(used<0 || used>=(int)sizeof(body)-3)return -1;
    snprintf(body+used,sizeof(body)-used,"]}");
    return text_request("POST","/ohnx/nds/swap",body,result,sizeof(result));
}
int net_upload(const char *local,const char *remote,const char *previous,const char *hash) {
    FILE *f=fopen(local,"rb"); if(!f)return -1;
    fseek(f,0,SEEK_END); long size=ftell(f); rewind(f);
    if(size<=0||size>1024*1024){fclose(f);return -1;}
    char encoded[1024],path[1100],extra[256];static char buffer[4096]; int result=-1,sock=-1;
    if(encode(remote,encoded,sizeof(encoded)))goto done;
    snprintf(path,sizeof(path),"/ohnx/upload?path=%s",encoded);
    snprintf(extra,sizeof(extra),"X-Content-SHA256: %s\r\nX-Previous-SHA256: %s\r\n",hash,*previous?previous:"missing");
    sock=begin("POST",path,size,extra);if(sock<0)goto done;
    size_t n;long sent=0;
    while((n=fread(buffer,1,sizeof(buffer),f))) {
        if(send_all(sock,buffer,n))goto done;
        sent+=n;
        if(progress_callback && (sent%16384==0 || sent==size))progress_callback("Uploading",sent,size);
    }
    if(ferror(f)||sent!=size)goto done;
    if(progress_callback)progress_callback("Waiting for hub response",0,0);
    size_t length;if(response(sock,&length)==200)result=0;
done: if(sock>=0)mini_socket_close(sock);fclose(f);return result;
}
int net_download(const char *remote,const char *temporary,const char *expected) {
    char encoded[1024],path[1100],hash[65];static char buffer[4096];size_t left;
    if(encode(remote,encoded,sizeof(encoded)))return -1;
    snprintf(path,sizeof(path),"/ohnx/raw?path=%s",encoded);
    int sock=begin("GET",path,0,NULL);if(sock<0)return -1;
    FILE *f=NULL;int result=-1;
    if(response(sock,&left)!=200 || !left)goto done;
    f=fopen(temporary,"wb");if(!f)goto done;
    size_t total=left;
    while(left) {size_t n=left<sizeof(buffer)?left:sizeof(buffer);if(read_all(sock,buffer,n)||fwrite(buffer,1,n,f)!=n)goto done;left-=n;
        if(progress_callback && ((total-left)%16384==0 || !left))progress_callback("Downloading",total-left,total);
    }
    if(fflush(f))goto done;
    if(fclose(f)){f=NULL;goto done;}f=NULL;
    if(!mini_sha_file(temporary,hash)&&!strcmp(hash,expected))result=0;
done: if(f)fclose(f);mini_socket_close(sock);return result;
}
