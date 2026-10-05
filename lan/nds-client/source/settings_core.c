#include "settings_core.h"
#include "mini_core.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
int settings_save_config(const char *path,const char *host,int port){
    unsigned a,b,c,d;char extra;
    if(strlen(path)>200||strlen(host)>15||port<1||port>65535||
       sscanf(host,"%u.%u.%u.%u%c",&a,&b,&c,&d,&extra)!=4||a!=192||b!=168||c!=1||d==0||d>254)return -1;
    char previous[65],current[65],backup[320],temporary[256];
    snprintf(temporary,sizeof(temporary),"%s.new",path);
    if(mini_sha_file(path,previous)||access(temporary,F_OK)==0)return -1;
    FILE *source=fopen(path,"r"),*dest=fopen(temporary,"w");
    if(!source||!dest){if(source)fclose(source);if(dest){fclose(dest);remove(temporary);}return -1;}
    char line[1024];int host_written=0,port_written=0,failed=0;
    while(fgets(line,sizeof(line),source)){
        if(!strncmp(line,"host=",5)){if(fprintf(dest,"host=%s\n",host)<0)failed=1;host_written=1;}
        else if(!strncmp(line,"port=",5)){if(fprintf(dest,"port=%d\n",port)<0)failed=1;port_written=1;}
        else if(fputs(line,dest)<0)failed=1;
    }
    if(ferror(source))failed=1;
    if(!host_written&&fprintf(dest,"\nhost=%s\n",host)<0)failed=1;
    if(!port_written&&fprintf(dest,"\nport=%d\n",port)<0)failed=1;
    if(fflush(dest))failed=1;
    if(fclose(dest))failed=1;
    fclose(source);
    if(failed||mini_sha_file(temporary,current)||mini_sha_file(path,current)||strcmp(previous,current)){remove(temporary);return -1;}
    snprintf(backup,sizeof(backup),"%s.backup.%lu",path,(unsigned long)time(NULL));
    for(unsigned i=0;access(backup,F_OK)==0;i++)snprintf(backup,sizeof(backup),"%s.backup.%lu-%u",path,(unsigned long)time(NULL),i);
    if(rename(path,backup)){remove(temporary);return -1;}
    if(rename(temporary,path)){rename(backup,path);remove(temporary);return -1;}
    return 0;
}
