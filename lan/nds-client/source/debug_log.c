/* Bounded flight recorder: persists stages before graphics/network work. */
#include "debug_log.h"
#include "mini_core.h"
#include "net.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

static int ready;
static unsigned sequence;
static char uploaded[65];
static const char *log_path="/openhome-mini.log";
void debug_log(const char *format,...) {
    if(!ready)return;
    char text[256];va_list args;va_start(args,format);
    vsnprintf(text,sizeof(text),format,args);va_end(args);
    FILE *file=fopen(log_path,"a");if(!file)return;
    fseek(file,0,SEEK_END);
    if(ftell(file)>128*1024){fclose(file);file=fopen(log_path,"w");if(!file)return;}
    fprintf(file,"%lu #%u %s\n",(unsigned long)time(NULL),++sequence,text);
    fflush(file);fclose(file);
}
void debug_init(const char *version) {
    FILE *source=fopen(log_path,"rb");
    if(source){
        FILE *snapshot=fopen("/openhome-mini.previous.log","wb");
        if(snapshot){char buffer[512];size_t n,total=0;
            while(total<128*1024 && (n=fread(buffer,1,sizeof(buffer),source))){
                if(fwrite(buffer,1,n,snapshot)!=n)break;
                total+=n;
            }
            fflush(snapshot);fclose(snapshot);
        }
        fclose(source);
    }
    FILE *file=fopen(log_path,"w");if(file){fclose(file);ready=1;}
    debug_log("BOOT version=%s",version);
}
void debug_upload_previous(const char *card_id) {
    char sha[65],remote[256];
    if(mini_sha_file("/openhome-mini.previous.log",sha))return;
    if(!strcmp(uploaded,sha))return;
    if(!*card_id || strchr(card_id,'/') || strchr(card_id,'\\') || strlen(card_id)>95)return;
    snprintf(remote,sizeof(remote),"diagnostics/%s/%.16s.log",card_id,sha);
    debug_log("Previous-session log upload begin");
    /* Immutable snapshot; the active log continues recording to a different file. */
    int result=net_upload("/openhome-mini.previous.log",remote,"",sha);
    if(!result)snprintf(uploaded,sizeof(uploaded),"%s",sha);
    debug_log("Previous-session log upload result=%d",result);
}
