#include "port_ui.h"
#include "net.h"
#include "theme.h"
#include "debug_log.h"
#include <dswifi9.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

extern const uint8_t *port_icon_get(unsigned species);
extern const uint16_t port_icon_palette[256];
static uint16_t *graphics[30];
static uint16_t *portrait;
static unsigned char view[7809];
static int cursor, box_index, picked_box, picked_slot, stats_page;
static char picked_sha[65],status_text[96];
static const char *natures[25]={"Hardy","Lonely","Brave","Adamant","Naughty","Bold","Docile","Relaxed","Impish","Lax","Timid","Hasty","Serious","Jolly","Naive","Modest","Mild","Quiet","Bashful","Rash","Calm","Gentle","Sassy","Careful","Quirky"};
static unsigned read16(const unsigned char *p){return p[0]|(p[1]<<8);}
static void hash_hex(char out[65]) {for(int i=0;i<32;i++)sprintf(out+2*i,"%02x",view[8+i]);out[64]=0;}
static void hide_portrait(void){
    /* Affine sprites share their hide bit with the double-size flag. */
    oamSetAffineIndex(&oamMain,0,-1,false);
    oamSetHidden(&oamMain,0,true);
}
static void hide_icons(void){for(int i=0;i<30;i++)oamSetHidden(&oamSub,i,true);hide_portrait();swiWaitForVBlank();oamUpdate(&oamSub);oamUpdate(&oamMain);}
void port_init(void){
    vramSetBankD(VRAM_D_SUB_SPRITE);
    vramSetBankB(VRAM_B_MAIN_SPRITE);
    oamInit(&oamSub,SpriteMapping_1D_32,false);
    oamInit(&oamMain,SpriteMapping_1D_32,false);
    for(int i=0;i<30;i++)graphics[i]=oamAllocateGfx(&oamSub,SpriteSize_32x32,SpriteColorFormat_256Color);
    portrait=oamAllocateGfx(&oamMain,SpriteSize_32x32,SpriteColorFormat_256Color);
    dmaCopy(port_icon_palette,SPRITE_PALETTE,512);dmaCopy(port_icon_palette,SPRITE_PALETTE_SUB,512);
    oamAffineTransformation(&oamMain,0,128,0,0,128);
    hide_icons();
}
static void render(PrintConsole *top,PrintConsole *bottom){
    debug_log("BOX render begin box=%d slot=%d stats=%d",box_index,cursor,stats_page);
    unsigned char *p=view+128+cursor*256;
    theme_boxes(cursor,picked_box==box_index?picked_slot:-1,stats_page);
    debug_log("BOX background ready");
    consoleSelect(top);consoleClear();
    iprintf("\x1b[2;2HOPENHOME\x1b[36m   %s\x1b[0m",stats_page?"STATS":"POKEMON");
    iprintf("\x1b[2;29H\x1b[%dm%c\x1b[0m",Wifi_AssocStatus()==ASSOCSTATUS_ASSOCIATED?32:33,127);
    hide_portrait();
    if(p[90]==1){
        unsigned species=read16(p);
        if(species<650&&portrait){debug_log("BOX portrait copy species=%u",species);dmaCopy(port_icon_get(species),portrait,1024);
            oamSet(&oamMain,0,8,32,0,0,SpriteSize_32x32,SpriteColorFormat_256Color,portrait,0,true,false,false,false,false);}
        iprintf("\x1b[5;11H\x1b[33m%.20s\x1b[0m",p+96);
        iprintf("\x1b[7;11HLv.%u  %s%s",p[2],p[5]==0?"M":p[5]==1?"F":"-",p[4]&1?"  * Shiny":"");
        iprintf("\x1b[9;11H%.20s",p[4]&2?"Egg":p[3]<25?natures[p[3]]:"?");
        iprintf("\x1b[12;11H\x1b[36m%.20s\x1b[0m",p+42);
        if(stats_page){
            iprintf("\x1b[15;3HOT %.25s",p+66);
            iprintf("\x1b[16;3H\x1b[36mHP AT DF SP SA SD\x1b[0m");
            iprintf("\x1b[17;3HIV %u/%u/%u/%u/%u/%u",p[18],p[19],p[20],p[21],p[22],p[23]);
            iprintf("\x1b[19;3HEV %u/%u/%u/%u/%u/%u",p[24],p[25],p[26],p[27],p[28],p[29]);
            iprintf("\x1b[21;3HID %u   SID %u",read16(p+34),read16(p+36));
        }else{
            iprintf("\x1b[15;3H%.27s\x1b[17;3HItem %.22s",p+184,p+208);
            iprintf("\x1b[19;3H%.13s / %.13s\x1b[20;3H%.13s / %.13s",p+120,p+136,p+152,p+168);
        }
    }else iprintf("\x1b[7;3HSlot %d: %s\x1b[15;3H%.27s\x1b[17;3HTrainer %.20s",cursor+1,p[90]==2?"Damaged record":"Empty",view+40,view+72);
    iprintf("\x1b[23;2H%.30s",status_text);
    consoleSelect(bottom);consoleClear();
    debug_log("BOX text ready; drawing grid sprites");
    iprintf("\x1b[2;2H<  BOX %02d/%02u       SLOT %02d  >",box_index+1,view[5],cursor+1);
    iprintf("\x1b[22;2HA %s  X Cancel  Y Details\x1b[24;2HL/R Box  Touch Select  B Back",picked_slot>=0?"Place":"Pick ");
    for(int i=0;i<30;i++){
        unsigned char *mon=view+128+i*256;unsigned species=read16(mon);
        int x=11+(i%6)*40,y=26+(i/6)*26;
        if(mon[90]==1&&species<650&&graphics[i]){
            if(i==cursor)debug_log("BOX selected sprite copy slot=%d species=%u",i,species);
            dmaCopy(port_icon_get(species),graphics[i],1024);
            oamSet(&oamSub,i,x,y,0,0,SpriteSize_32x32,SpriteColorFormat_256Color,graphics[i],-1,false,false,false,false,false);
        }else oamSetHidden(&oamSub,i,true);
        if(mon[90]==2)iprintf("\x1b[%d;%dH!",5+(i/6)*3,3+(i%6)*5);
    }
    debug_log("BOX sprites ready; updating OAM");
    swiWaitForVBlank();oamUpdate(&oamSub);oamUpdate(&oamMain);
    debug_log("BOX render complete");
}
static int load(const char *remote,PrintConsole *top){
    debug_log("BOX load begin remote=%s box=%d",remote,box_index);
    hide_icons();consoleSelect(top);consoleClear();
    theme_dialog(0);
    iprintf("\x1b[2;3HOPENHOME\x1b[7;3HLoading box %d...\x1b[11;3HB cancels a network wait.",box_index+1);
    if(net_box(remote,box_index,(char*)view,sizeof(view)))return -1;
    debug_log("BOX response validated");
    return 0;
}
static int confirm_move(PrintConsole *top){
    consoleSelect(top);consoleClear();
    hide_icons();theme_dialog(0);
    unsigned char *p=view+128+cursor*256;
    iprintf("\x1b[2;3HCONFIRM %s\x1b[7;3HFrom: box %d slot %d\x1b[10;3HTo: box %d slot %d\x1b[14;3HOld save backup will be kept.\x1b[23;3HA Confirm            B Cancel",p[90]==1?"SWAP":"MOVE",picked_box+1,picked_slot+1,box_index+1,cursor+1);
    while(1){swiWaitForVBlank();scanKeys();int keys=keysDown();if(keys&KEY_A)return 1;if(keys&(KEY_B|KEY_START))return 0;}
}
void port_browser(PrintConsole *top,PrintConsole *bottom,const char *remote){
    cursor=box_index=stats_page=0;picked_slot=-1;
    snprintf(status_text,sizeof(status_text),"Browsing hub save.");
    if(load(remote,top))goto error;
    render(top,bottom);
    while(1){
        swiWaitForVBlank();scanKeys();int keys=keysDown();int changed=0;
        if(keys&(KEY_B|KEY_START))break;
        if(keys&KEY_LEFT){cursor=(cursor+29)%30;changed=1;}
        if(keys&KEY_RIGHT){cursor=(cursor+1)%30;changed=1;}
        if(keys&KEY_UP){cursor=(cursor+24)%30;changed=1;}
        if(keys&KEY_DOWN){cursor=(cursor+6)%30;changed=1;}
        if(keys&KEY_Y){stats_page=!stats_page;changed=1;}
        if(keys&KEY_TOUCH){touchPosition touch;touchRead(&touch);
            if(touch.py<24){if(touch.px<40)keys|=KEY_L;else if(touch.px>=216)keys|=KEY_R;}
            if(touch.px>=8&&touch.px<248&&touch.py>=30&&touch.py<160){
                int row=(touch.py-30)/26,col=(touch.px-8)/40;
                if(row<5&&col<6){cursor=row*6+col;changed=1;}
            }
        }
        if(keys&(KEY_L|KEY_R)){
            box_index=(box_index+((keys&KEY_R)?1:view[5]-1))%view[5];
            if(load(remote,top))goto error;
            changed=1;
        }
        if(keys&KEY_X){picked_slot=-1;snprintf(status_text,sizeof(status_text),"Move cancelled.");changed=1;}
        if(keys&KEY_A){
            if(picked_slot<0){
                if(view[128+cursor*256+90]==1){picked_slot=cursor;picked_box=box_index;hash_hex(picked_sha);
                    snprintf(status_text,sizeof(status_text),"Choose destination. A places.");}
                else snprintf(status_text,sizeof(status_text),"Select a valid Pokemon first.");
            }else if(picked_box==box_index&&picked_slot==cursor){picked_slot=-1;snprintf(status_text,sizeof(status_text),"Move cancelled.");}
            else if(confirm_move(top)){
                char current[65];hash_hex(current);
                if(strcmp(current,picked_sha)){snprintf(status_text,sizeof(status_text),"Save changed. Pick the Pokemon again.");picked_slot=-1;}
                else if(net_swap(remote,picked_sha,picked_box,picked_slot,box_index,cursor)){
                    snprintf(status_text,sizeof(status_text),"%.95s",net_error);picked_slot=-1;
                    if(load(remote,top))goto error;
                }else{
                    picked_slot=-1;
                    snprintf(status_text,sizeof(status_text),"Moved. B returns and syncs card.");
                    if(load(remote,top))goto error;
                }
            }
            changed=1;
        }
        if(changed)render(top,bottom);
    }
    hide_icons();return;
error:
    hide_icons();consoleSelect(top);consoleClear();consoleSelect(bottom);consoleClear();
    theme_dialog(0);theme_dialog(1);
    iprintf("\x1b[2;3HOPENHOME\x1b[7;3H%.28s\x1b[9;3H%.28s\x1b[11;3H%.28s\x1b[23;3HB Return to saves",net_error,strlen(net_error)>28?net_error+28:"",strlen(net_error)>56?net_error+56:"");
    while(1){swiWaitForVBlank();scanKeys();if(keysDown()&(KEY_B|KEY_START))break;}
}
