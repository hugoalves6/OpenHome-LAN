#include "settings_ui.h"
#include "settings_core.h"
#include "theme.h"
#include "net.h"
#include "mini_core.h"
#include "debug_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void top_page(PrintConsole *top,const char *title,const char *version){
    consoleSelect(top);consoleClear();theme_dialog(0);
    char label[64];snprintf(label,sizeof(label),"OPENHOME  %s",title);theme_text(top,16,8,37,label);
    theme_text(top,16,40,37,"Server");theme_text(top,16,56,36,hub_host);
    snprintf(label,sizeof(label),"Port %d",hub_port);theme_text(top,16,72,37,label);
    theme_text(top,16,112,37,"Wi-Fi uses the console's");theme_text(top,16,120,37,"saved network connection.");
    snprintf(label,sizeof(label),"DS CLIENT          v%s",version);theme_text(top,16,176,37,label);
}
static int keypad(PrintConsole *top,PrintConsole *bottom,char *value,size_t cap,int address){
    static const char keys[]="123456789.0<";
    char edited[32];snprintf(edited,sizeof(edited),"%s",value);
    int selected=0,redraw=1,fresh=1;
    while(1){
        if(redraw){
            theme_dialog(1);consoleSelect(bottom);consoleClear();
            theme_text(bottom,16,8,37,address?"EDIT SERVER IP":"EDIT SERVER PORT");
            theme_text(bottom,16,48,33,edited);
            for(int i=0;i<12;i++){char label[4];snprintf(label,sizeof(label),"[%c]",keys[i]);theme_text(bottom,32+(i%3)*80,72+(i/3)*24,i==selected?33:37,label);}
            theme_text(bottom,8,176,37,"START Done B Cancel X Delete");
            theme_text(bottom,16,184,37,"Done                 Cancel");redraw=0;
        }
        swiWaitForVBlank();scanKeys();int pressed=keysDown(),entry=-1;
        if(pressed&KEY_B)return 0;
        if(pressed&KEY_START){snprintf(value,cap,"%s",edited);return 1;}
        if(pressed&KEY_LEFT){selected=(selected+11)%12;redraw=1;}
        if(pressed&KEY_RIGHT){selected=(selected+1)%12;redraw=1;}
        if(pressed&KEY_UP){selected=(selected+9)%12;redraw=1;}
        if(pressed&KEY_DOWN){selected=(selected+3)%12;redraw=1;}
        if(pressed&KEY_A)entry=selected;
        if(pressed&KEY_X)entry=11;
        if(pressed&KEY_TOUCH){touchPosition touch;touchRead(&touch);
            if(touch.py>=168){if(touch.px<128){snprintf(value,cap,"%s",edited);return 1;}return 0;}
            if(touch.py>=64&&touch.py<160&&touch.px>=8&&touch.px<248){
                int row=(touch.py-64)/24,col=(touch.px-8)/80;
                if(row<4&&col<3){selected=row*3+col;entry=selected;}
            }
        }
        if(entry>=0){char key=keys[entry];
            if(fresh&&key!='<')edited[0]=0;
            fresh=0;size_t length=strlen(edited);
            if(key=='<'){if(length)edited[length-1]=0;}
            else if((address||key!='.')&&length+1<cap&&length+1<sizeof(edited)){edited[length]=key;edited[length+1]=0;}
            redraw=1;
        }
    }
}
int settings_open(PrintConsole *top,PrintConsole *bottom,const char *version){
    int selected=0,redraw=1;char host[16],port[6],message[64]="";
    snprintf(host,sizeof(host),"%.15s",hub_host);snprintf(port,sizeof(port),"%d",hub_port);
    while(1){
        if(redraw){theme_saves(selected,4);top_page(top,"SETTINGS",version);
            consoleSelect(bottom);consoleClear();theme_text(bottom,16,8,37,"CONNECTION SETTINGS");
            const char *names[]={"Server IP address","Server port","Save and connect","Back to home"};
            for(int i=0;i<4;i++){
                theme_text(bottom,24,40+i*32,i==selected?33:37,names[i]);
                if(i<2)theme_text(bottom,24,48+i*32,36,i==0?host:port);
            }
            theme_text(bottom,8,176,37,*message?message:"Changes keep your save mappings");theme_text(bottom,8,184,37,"A Select Touch Edit B Back");redraw=0;
        }
        swiWaitForVBlank();scanKeys();int keys=keysDown(),activate=keys&KEY_A;
        if(keys&(KEY_B|KEY_START))return 0;
        if(keys&KEY_UP){selected=(selected+3)%4;redraw=1;}
        if(keys&KEY_DOWN){selected=(selected+1)%4;redraw=1;}
        if(keys&KEY_TOUCH){touchPosition touch;touchRead(&touch);
            if(touch.py>=32&&touch.py<160){selected=(touch.py-32)/32;activate=1;redraw=1;}}
        if(activate){
            if(selected==0)keypad(top,bottom,host,sizeof(host),1);
            if(selected==1)keypad(top,bottom,port,sizeof(port),0);
            if(selected==3)return 0;
            if(selected==2){
                unsigned a,b,c,d;char extra;int number=atoi(port);
                if(sscanf(host,"%u.%u.%u.%u%c",&a,&b,&c,&d,&extra)!=4||a!=192||b!=168||c!=1||d==0||d>254)
                    snprintf(message,sizeof(message),"Use IP 192.168.1.1 to .254");
                else if(number<1||number>65535)snprintf(message,sizeof(message),"Port must be 1 to 65535");
                else if(settings_save_config("/openhome-mini.ini",host,number))snprintf(message,sizeof(message),"Settings save failed.");
                else {debug_log("SETTINGS server changed host=%s port=%d",host,number);snprintf(hub_host,sizeof(hub_host),"%s",host);hub_port=number;hub_token[0]=0;return 1;}
            }
            redraw=1;
        }
    }
}
void settings_diagnostics(PrintConsole *top,PrintConsole *bottom,const char *version){
    top_page(top,"DIAGNOSTICS",version);theme_dialog(1);consoleSelect(bottom);consoleClear();
    iprintf("\x1b[2;3HDIAGNOSTICS\x1b[6;3HCard log:\x1b[8;3H/openhome-mini.log\x1b[11;3HPrevious session:\x1b[13;3H/openhome-mini.previous.log\x1b[16;3HPrevious logs go to the Pi\x1b[18;3Hon the next connection.\x1b[23;3HB Back to home");
    while(1){swiWaitForVBlank();scanKeys();if(keysDown()&(KEY_B|KEY_START))return;}
}
