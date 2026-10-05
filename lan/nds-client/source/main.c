#include <nds.h>
#include <dswifi9.h>
#include <fat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "mini_core.h"
#include "net.h"
#include "port_ui.h"
#include "theme.h"
#include "debug_log.h"
#include "settings_ui.h"

#define MAX_SAVES 8
#define APP_VERSION "0.4.1"
typedef struct {
    char title[40], local[256], remote[256];
    char local_sha[65], hub_sha[65], baseline[65], checked[65], status[64];
    int examined;
} Save;
static Save saves[MAX_SAVES];
static int count=0, selected=0, connected=0, wifi_started=0;
static char listing[32768], message[96]="Starting...", card_id[96]="";
static PrintConsole top, bottom;
enum { PAGE_HOME, PAGE_SAVES, PAGE_BOX_PICK };
static int page=PAGE_HOME,home_selection;
static int update_pending;
static const char *home_items[]={"My saves","Pokemon boxes","Connection settings","App updates","Diagnostics","Exit"};
static unsigned short icon_font[1024];
static void draw_wifi(void) {
    int state=wifi_started?Wifi_AssocStatus():ASSOCSTATUS_DISCONNECTED;
    const char *status=state==ASSOCSTATUS_ASSOCIATED?"Connected":
        state==ASSOCSTATUS_SEARCHING?"Searching":
        state==ASSOCSTATUS_ASSOCIATING?"Connecting":
        state==ASSOCSTATUS_ACQUIRINGDHCP?"Getting IP":"Disconnected";
    char label[64];
    snprintf(label,sizeof(label),"%c Wi-Fi: %s",127,status);
    theme_text(&top,16,40,state==ASSOCSTATUS_ASSOCIATED?32:33,label);
    unsigned long ip=state==ASSOCSTATUS_ASSOCIATED?Wifi_GetIP():0;
    if(ip){snprintf(label,sizeof(label),"IP: %lu.%lu.%lu.%lu",ip&255,(ip>>8)&255,(ip>>16)&255,(ip>>24)&255);theme_text(&top,16,56,37,label);}
    consoleSelect(&bottom);
}

static void draw(void) {
    int first=(selected/4)*4,rows=count-first;if(rows>4)rows=4;
    if(page==PAGE_HOME)theme_home(home_selection);
    else theme_saves(selected-first,rows);
    consoleSelect(&top); consoleClear();
    char label[96];
    theme_text(&top,16,8,37,"OPENHOME              DS");
    snprintf(label,sizeof(label),"Hub  %s",connected?"Connected":"Offline");theme_text(&top,16,96,37,label);
    snprintf(label,sizeof(label),"%s:%d",hub_host,hub_port);theme_text(&top,16,112,36,label);
    snprintf(label,sizeof(label),"%.28s",message);theme_text(&top,16,136,37,label);
    if(strlen(message)>28)theme_text(&top,16,144,37,message+28);
    snprintf(label,sizeof(label),"LAN SAVE HUB       v%s",APP_VERSION);theme_text(&top,16,176,37,label);
    consoleSelect(&bottom);consoleClear();
    if(page==PAGE_HOME){
        snprintf(label,sizeof(label),"HOME                 %d GAMES",count);theme_text(&bottom,16,8,37,label);
        for(int i=0;i<6;i++)theme_home_label(&bottom,i,i==home_selection?33:37,home_items[i]);
        theme_text(&bottom,8,176,37,"A Open  Touch Select");
        theme_text(&bottom,8,184,37,"START Exit");
        draw_wifi();return;
    }
    snprintf(label,sizeof(label),"%s         %d/%d",page==PAGE_SAVES?"MY SAVES":"CHOOSE GAME",selected+1,count);theme_text(&bottom,16,8,37,label);
    for(int i=first;i<first+rows;i++){
        int y=40+(i-first)*32;
        theme_text(&bottom,24,y,i==selected?33:37,saves[i].title);
        theme_text(&bottom,24,y+8,36,saves[i].status);
    }
    theme_text(&bottom,8,176,37,page==PAGE_BOX_PICK?"A Open boxes  Touch Select":"SELECT Boxes A Sync R Update");
    theme_text(&bottom,8,184,37,page==PAGE_BOX_PICK?"L Reconnect  B Home":"X Up Y Down L Wi-Fi B Home");
    draw_wifi();
}
static void say(const char *text) { debug_log("STATUS %s",text);snprintf(message,sizeof(message),"%s",text);draw(); }
static void transfer_progress(const char *stage,size_t done,size_t total) {
    char text[96];
    if(total)snprintf(text,sizeof(text),"%s %lu%%",stage,(unsigned long)(done*100/total));
    else snprintf(text,sizeof(text),"%s",stage);
    /* Percentage callbacks redraw without logging every network chunk. */
    snprintf(message,sizeof(message),"%s",text);draw();
}
static int confirm(const char *text) {
    say(text);
    consoleSelect(&bottom); consoleClear();
    theme_dialog(1);
    iprintf("\x1b[2;3HCONFIRM\x1b[6;3H%.27s\x1b[10;3H%.28s\x1b[11;3H%.28s\x1b[22;3HA Confirm            B Cancel",saves[selected].title,text,strlen(text)>28?text+28:"");
    while(1){swiWaitForVBlank();scanKeys();int keys=keysDown();if(keys&KEY_A)return 1;if(keys&(KEY_B|KEY_START))return 0;}
}
static int setting(char *out,size_t capacity,const char *value) {
    if(strlen(value)>=capacity)return -1;
    memcpy(out,value,strlen(value)+1);return 0;
}
static int configure(void) {
    FILE *f=fopen("/openhome-mini.ini","r");if(!f)return -1;
    char line[1024];
    while(fgets(line,sizeof(line),f)){
        line[strcspn(line,"\r\n")]=0;
        if(!strncmp(line,"host=",5)){if(setting(hub_host,sizeof(hub_host),line+5)){fclose(f);return -1;}}
        else if(!strncmp(line,"port=",5))hub_port=atoi(line+5);
        else if(!strncmp(line,"password=",9)){if(setting(hub_password,sizeof(hub_password),line+9)){fclose(f);return -1;}}
        else if(!strncmp(line,"cardid=",7)){if(setting(card_id,sizeof(card_id),line+7)){fclose(f);return -1;}}
        else if(!strncmp(line,"save=",5)&&count<MAX_SAVES){
            char *title=line+5,*local=strchr(title,'|');if(!local)continue;*local++=0;
            char *remote=strchr(local,'|');if(!remote)continue;*remote++=0;
            if(strlen(local)>=sizeof(saves[0].local)-64 || strlen(remote)>=sizeof(saves[0].remote) ||
               strncmp(remote,"roms/nds/",9) || strchr(remote+9,'/') || strstr(remote,"..") ||
               strlen(local)<4 || strcmp(local+strlen(local)-4,".sav"))continue;
            Save *s=&saves[count++];snprintf(s->title,sizeof(s->title),"%.39s",title);
            snprintf(s->local,sizeof(s->local),"%s",local);snprintf(s->remote,sizeof(s->remote),"%s",remote);
            snprintf(s->status,sizeof(s->status),"Not checked");
        }
    }
    fclose(f);
    if(hub_port<1||hub_port>65535||!count)return -1;
    if(*card_id){
        f=fopen("/.ohnx-device-id","r");if(!f)return -1;
        char identity[128]={0};fgets(identity,sizeof(identity),f);fclose(f);
        identity[strcspn(identity,"\r\n")]=0;
        if(strcmp(identity,card_id))return -1;
    }
    for(int i=0;i<count;i++){
        char path[320];snprintf(path,sizeof(path),"%s.ohnx-baseline",saves[i].local);
        f=fopen(path,"r");if(f){fgets(saves[i].baseline,65,f);fclose(f);if(strlen(saves[i].baseline)!=64)saves[i].baseline[0]=0;}
    }
    return 0;
}
static int remember(Save *s,const char *hash) {
    char path[320];snprintf(path,sizeof(path),"%s.ohnx-baseline",s->local);
    FILE *f=fopen(path,"w");if(!f)return -1;
    int failed=fputs(hash,f)<0 || fflush(f);if(fclose(f))failed=1;
    if(failed)return -1;
    snprintf(s->baseline,sizeof(s->baseline),"%s",hash);return 0;
}
static int hub_hash(Save *s) {
    const char *name=strrchr(s->remote,'/');
    return mini_listing_sha(listing,name?name+1:s->remote,s->hub_sha);
}
static int receive_save(Save *s) {
    char temporary[320],backup[320],current[65];
    snprintf(temporary,sizeof(temporary),"%s.ohnx-new",s->local);
    snprintf(backup,sizeof(backup),"%s.ohnx-backup.%lu",s->local,(unsigned long)time(NULL));
    /* Never replace an earlier backup when the DS clock has not changed. */
    for(unsigned i=0;access(backup,F_OK)==0;i++)snprintf(backup,sizeof(backup),"%s.ohnx-backup.%lu-%u",s->local,(unsigned long)time(NULL),i);
    if(net_download(s->remote,temporary,s->hub_sha))return -1;
    if(mini_sha_file(s->local,current)||strcmp(current,s->local_sha))return -1;
    if(rename(s->local,backup))return -1;
    if(rename(temporary,s->local)) {rename(backup,s->local);return -1;}
    if(mini_sha_file(s->local,current)||strcmp(current,s->hub_sha))return -1;
    snprintf(s->local_sha,sizeof(s->local_sha),"%s",current);
    return remember(s,current);
}
static int sync_save(Save *s,int forced) {
    debug_log("SYNC begin remote=%s forced=%d",s->remote,forced);
    if(mini_sha_file(s->local,s->local_sha)){snprintf(s->status,sizeof(s->status),"Save unreadable");return -1;}
    if(hub_hash(s)<0){snprintf(s->status,sizeof(s->status),"Invalid hub checksum");return -1;}
    int action=forced?forced:mini_action(s->local_sha,s->hub_sha,s->baseline);
    if(action==0){if(remember(s,s->local_sha))return -1;snprintf(s->status,sizeof(s->status),"Up to date");}
    else if(action==3)snprintf(s->status,sizeof(s->status),"Conflict - choose X or Y");
    else if(action==1){
        say("Uploading selected save...");
        if(net_upload(s->local,s->remote,s->hub_sha,s->local_sha))return -1;
        say("Upload sent. Verifying hub copy...");
        if(net_listing(listing,sizeof(listing))||hub_hash(s)<0||strcmp(s->local_sha,s->hub_sha))return -1;
        say("Verified. Saving sync status...");
        if(remember(s,s->local_sha))return -1;
        snprintf(s->status,sizeof(s->status),"Uploaded and verified");
    }else if(action==2){
        if(!*s->hub_sha){snprintf(s->status,sizeof(s->status),"No hub copy");return -1;}
        say("Downloading and backing up...");
        if(receive_save(s))return -1;
        snprintf(s->status,sizeof(s->status),"Downloaded - backup kept");
    }
    snprintf(s->checked,sizeof(s->checked),"%s",s->hub_sha);s->examined=1;return 0;
}
static void live_pulse(int closing){
    static char body[2048];
    int used=snprintf(body,sizeof(body),"{\"card_id\":\"%s\",\"session\":\"%s\",\"saves\":[",card_id,hub_token);
    int entries=0;
    if(!closing)for(int i=0;i<count;i++){
        Save *s=&saves[i];
        if(strlen(s->local_sha)!=64||!s->examined||!*s->hub_sha)continue;
        int n=snprintf(body+used,sizeof(body)-used,"%s{\"path\":\"%s\",\"sha256\":\"%s\"}",entries?",":"",s->remote,s->local_sha);
        if(n<0||n>=(int)sizeof(body)-used)return;
        used+=n;entries++;
    }
    if(used+3>=(int)sizeof(body))return;
    strcpy(body+used,"]}");
    if(net_live_pulse(body))debug_log("Live presence report failed");
}
static int refresh(int full) {
    if(!connected)return -1;
    if(net_listing(listing,sizeof(listing))) {
        if(net_login()||net_listing(listing,sizeof(listing))){connected=0;say("Hub unavailable. L to retry.");return -1;}
    }
    for(int i=0;i<count;i++){
        Save *s=&saves[i];char old[65];snprintf(old,sizeof(old),"%s",s->checked);
        if(hub_hash(s)<0)return -1;
        if(!*s->hub_sha){
            snprintf(s->status,sizeof(s->status),"No hub copy - A to upload");
            s->checked[0]=0;s->examined=1;continue;
        }
        if(full||!s->examined||strcmp(old,s->hub_sha)) {
            if(sync_save(s,0)){snprintf(s->status,sizeof(s->status),"Transfer failed - press A");
                /* Do not repeatedly send a failed operation every 15s. */
                snprintf(s->checked,sizeof(s->checked),"%s",s->hub_sha);s->examined=1;
            }
        }
    }
    live_pulse(0);say("Ready. Watching hub changes.");return 0;
}
static void update_app(int automatic) {
    if(update_pending){say("Update installed. Restart app.");return;}
    char info[512],version[40],hash[65],current[65];
    say("Checking for app update...");
    if(net_update_info(info,sizeof(info)) || mini_json_string(info,"version",version,sizeof(version)) ||
       mini_json_string(info,"sha256",hash,sizeof(hash)) || strlen(hash)!=64){say("Update check failed. L to retry.");return;}
    if(!strcmp(version,APP_VERSION)){if(!automatic)say("App is already up to date.");return;}
    if(!confirm("Install app update?"))return;
    say("Downloading update. B cancels.");
    if(net_download("updates/OpenHomeMini.nds","/OpenHomeMini.nds.new",hash)) {say("Update failed. Old app retained.");return;}
    if(mini_sha_file("/OpenHomeMini.nds",current)){say("App file missing. Update stopped.");return;}
    char backup[96];snprintf(backup,sizeof(backup),"/OpenHomeMini.nds.backup.%lu",(unsigned long)time(NULL));
    for(unsigned i=0;access(backup,F_OK)==0;i++)snprintf(backup,sizeof(backup),"/OpenHomeMini.nds.backup.%lu-%u",(unsigned long)time(NULL),i);
    if(rename("/OpenHomeMini.nds",backup)){say("Cannot back up app. Update stopped.");return;}
    if(rename("/OpenHomeMini.nds.new","/OpenHomeMini.nds")){rename(backup,"/OpenHomeMini.nds");say("Install failed. Backup retained.");return;}
    update_pending=1;say("Update installed. Restart app.");
}
static void reconnect(void) {
    connected=0;say("Connecting to Wi-Fi...");
    if(!wifi_started){if(!Wifi_InitDefault(INIT_ONLY)){say("Wi-Fi initialization failed.");return;}wifi_started=1;}
    if(Wifi_AssocStatus()!=ASSOCSTATUS_ASSOCIATED){
        Wifi_AutoConnect();
        int frames=0;
        while(Wifi_AssocStatus()!=ASSOCSTATUS_ASSOCIATED&&frames++<1800){swiWaitForVBlank();scanKeys();if(!(frames%30))draw_wifi();if(keysDown()&KEY_B)break;}
        if(Wifi_AssocStatus()!=ASSOCSTATUS_ASSOCIATED){say("Wi-Fi unavailable. L to retry.");return;}
    }
    say("Connecting to your hub...");
    if(net_login()){say("Login failed. Check card config.");return;}
    connected=1;
    debug_upload_previous(card_id);
    update_app(1);refresh(1);
    if(update_pending)say("Update installed. Restart app.");
}
int main(void) {
    defaultExceptionHandler();
    videoSetMode(MODE_5_2D);videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);vramSetBankC(VRAM_C_SUB_BG);
    consoleInit(&top,0,BgType_Text4bpp,BgSize_T_256x256,4,0,true,true);
    consoleInit(&bottom,0,BgType_Text4bpp,BgSize_T_256x256,4,0,false,true);
    /* Reserve glyph 127 for an 8x8 Wi-Fi icon in the existing 1bpp font. */
    if(top.font.bpp==1 && top.font.numChars==256 && top.font.asciiOffset==0){
        memcpy(icon_font,top.font.gfx,sizeof(icon_font));
        const unsigned char icon[8]={0x7e,0x81,0x3c,0x42,0x18,0x24,0x00,0x18};
        memcpy((unsigned char*)icon_font+127*8,icon,8);
        ConsoleFont font=top.font;font.gfx=icon_font;consoleSetFont(&top,&font);
    }
    theme_init(&top,&bottom);
    int storage_ready=fatInitDefault();
    if(storage_ready)debug_init(APP_VERSION);
    debug_log("Storage mounted; reading configuration");
    if(!storage_ready||configure()){say("Card config/storage not ready.");while(1){swiWaitForVBlank();scanKeys();if(keysDown()&KEY_START)return 0;}}
    debug_log("Configuration loaded; save_count=%d",count);
    net_set_progress(transfer_progress);
    port_init();
    debug_log("Sprite initialization complete");
    reconnect();int frames=0;
    while(1){
        swiWaitForVBlank();scanKeys();int keys=keysDown();
        if(!(frames%60))draw_wifi();
        if(keys&KEY_START)break;
        if(page==PAGE_HOME){
            int activate=keys&KEY_A,quit=0;
            if(keys&KEY_UP){home_selection=(home_selection+5)%6;draw();}
            if(keys&KEY_DOWN){home_selection=(home_selection+1)%6;draw();}
            if(keys&KEY_TOUCH){touchPosition touch;touchRead(&touch);
                int hit=theme_home_hit(touch.px,touch.py);if(hit>=0){home_selection=hit;activate=1;}}
            if(keys&KEY_L){reconnect();frames=0;}
            if(keys&KEY_R){home_selection=3;activate=1;}
            if(activate){
                debug_log("HOME action=%d",home_selection);
                if(home_selection==0){page=PAGE_SAVES;say("Choose a game to sync.");}
                else if(home_selection==1){page=PAGE_BOX_PICK;say("Choose a game to browse.");}
                else if(home_selection==2){
                    if(settings_open(&top,&bottom,APP_VERSION))reconnect();
                    else draw();
                }
                else if(home_selection==3){if(!connected)reconnect();if(connected)update_app(0);draw();}
                else if(home_selection==4){settings_diagnostics(&top,&bottom,APP_VERSION);draw();}
                else quit=1;
                frames=0;
            }
            if(quit)break;
            if(++frames>=300){frames=0;if(connected)refresh(0);}
            continue;
        }
        if(keys&KEY_B){page=PAGE_HOME;draw();continue;}
        if(keys&KEY_UP){selected=(selected+count-1)%count;draw();}
        if(keys&KEY_DOWN){selected=(selected+1)%count;draw();}
        if(keys&KEY_TOUCH){touchPosition touch;touchRead(&touch);
            if(touch.py>=32&&touch.py<160){int choice=(selected/4)*4+(touch.py-32)/32;if(choice<count){selected=choice;draw();}}
        }
        if(keys&KEY_L){reconnect();frames=0;}
        if(connected&&(keys&KEY_R)){update_app(0);frames=0;}
        if(connected&&((keys&KEY_SELECT)||(page==PAGE_BOX_PICK&&(keys&KEY_A)))){
            Save *s=&saves[selected];
            if(net_listing(listing,sizeof(listing))||hub_hash(s)<0||mini_sha_file(s->local,s->local_sha))say("Cannot check save. L to retry.");
            else if(strcmp(s->hub_sha,s->local_sha))say("Sync this save with A before boxes.");
            else {port_browser(&top,&bottom,s->remote);refresh(1);draw();}
            frames=0;
        }
        if(page==PAGE_SAVES&&connected&&(keys&(KEY_A|KEY_X|KEY_Y))){
            int forced=keys&KEY_X?1:keys&KEY_Y?2:0;
            if(!forced||confirm(forced==1?"Use card copy on hub?":"Replace card copy + keep backup?")){
                if(net_listing(listing,sizeof(listing))||sync_save(&saves[selected],forced))say("Transfer failed. Copies retained.");
                else say("Ready. Watching hub changes.");
            }frames=0;draw();
        }
        if(++frames>=300){frames=0;if(connected)refresh(0);}
    }
    if(connected)live_pulse(1);
    debug_log("EXIT normal");
    return 0;
}
