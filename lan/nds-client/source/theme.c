#include "theme.h"
#include <stdio.h>
#include <string.h>

/* Pixel positions are explicit: do not depend on ANSI cursor conventions.
   Reserve the last column so bottom-row text can never scroll the console. */
void theme_text(PrintConsole *console,int x,int y,int color,const char *text){
    int column=x/8,row=y/8;
    if(column<0||column>=31||row<0||row>=24)return;
    consoleSelect(console);console->cursorX=column;console->cursorY=row;
    iprintf("\x1b[%dm%.*s\x1b[0m",color,31-column,text);
}
static int home_y(int index){return 28+index*24;}
void theme_home_label(PrintConsole *console,int index,int color,const char *text){
    if(index>=0&&index<6)theme_text(console,24,home_y(index)+4,color,text);
}
int theme_home_hit(int x,int y){
    if(x<8||x>=248)return -1;
    for(int i=0;i<6;i++)if(y>=home_y(i)&&y<home_y(i)+16)return i;
    return -1;
}
static u16 *surfaces[2];
/* Native 8bpp backgrounds share VRAM with the text consoles. The console uses
   the first 64 KiB; the bitmap uses the second 64 KiB of each BG bank. */
static void pixel(int screen,int x,int y,unsigned color){
    if(x<0||x>=256||y<0||y>=192)return;
    u16 *p=surfaces[screen]+y*128+x/2;
    unsigned shift=(x&1)*8;
    *p=(*p&~(255u<<shift))|(color<<shift);
}
static void rect(int screen,int x,int y,int width,int height,unsigned color){
    for(int row=y;row<y+height&&row<192;row++){
        if(row<0)continue;
        int left=x,right=x+width;if(left<0)left=0;if(right>256)right=256;
        if(left&1){pixel(screen,left++,row,color);}
        for(int col=left;col+1<right;col+=2)surfaces[screen][row*128+col/2]=color|(color<<8);
        if(right>left&&(right&1))pixel(screen,right-1,row,color);
    }
}
static void border(int screen,int x,int y,int w,int h,unsigned color){
    rect(screen,x,y,w,1,color);rect(screen,x,y+h-1,w,1,color);
    rect(screen,x,y,1,h,color);rect(screen,x+w-1,y,1,h,color);
}
static void base(int screen){
    rect(screen,0,0,256,192,1);
    rect(screen,0,0,256,24,2);
    rect(screen,0,24,256,1,5);
    rect(screen,0,168,256,24,2);
    rect(screen,0,168,256,1,5);
}
void theme_init(PrintConsole *top,PrintConsole *bottom){
    int main_bg=bgInit(3,BgType_Bmp8,BgSize_B8_256x256,4,0);
    int sub_bg=bgInitSub(3,BgType_Bmp8,BgSize_B8_256x256,4,0);
    bgSetPriority(main_bg,3);bgSetPriority(sub_bg,3);
    bgSetPriority(top->bgId,0);bgSetPriority(bottom->bgId,0);
    surfaces[0]=(u16*)bgGetGfxPtr(main_bg);surfaces[1]=(u16*)bgGetGfxPtr(sub_bg);
    u16 colors[15]={0,RGB15(1,10,10),RGB15(1,7,8),RGB15(7,15,15),RGB15(9,18,18),
        RGB15(12,21,21),RGB15(31,25,9),RGB15(14,29,24),RGB15(16,23,23),RGB15(7,23,22),
        RGB15(27,30,29),RGB15(23,12,13),RGB15(4,12,13),RGB15(9,19,19),RGB15(14,24,24)};
    for(int i=0;i<15;i++){BG_PALETTE[i]=colors[i];BG_PALETTE_SUB[i]=colors[i];}
    /* Single-colour console glyphs occupy index 15 in each palette bank. */
    for(int bank=0;bank<16;bank++){
        u16 color=RGB15(29,31,30);
        if(bank==1)color=RGB15(31,16,16);
        if(bank==2)color=colors[7];
        if(bank==3)color=colors[6];
        if(bank==6)color=RGB15(18,30,28);
        BG_PALETTE[bank*16+15]=color;BG_PALETTE_SUB[bank*16+15]=color;
    }
    base(0);base(1);bgUpdate();
}
void theme_saves(int selected_row,int rows){
    base(0);base(1);
    rect(0,8,32,240,48,3);border(0,8,32,240,48,5);
    rect(0,8,88,240,32,3);border(0,8,88,240,32,5);
    rect(0,8,128,240,32,12);border(0,8,128,240,32,5);
    for(int i=0;i<rows;i++){
        int y=32+i*32;
        rect(1,8,y,240,28,i==selected_row?4:3);
        border(1,8,y,240,28,i==selected_row?6:5);
        if(i==selected_row)rect(1,8,y,3,28,6);
    }
}
void theme_boxes(int selected_slot,int picked_slot,int stats){
    base(0);base(1);
    rect(0,8,32,240,72,3);border(0,8,32,240,72,5);
    rect(0,8,112,240,48,stats?12:3);border(0,8,112,240,48,5);
    for(int i=0;i<30;i++){
        int x=8+(i%6)*40,y=30+(i/6)*26;
        rect(1,x,y,38,24,i==selected_slot?4:3);
        border(1,x,y,38,24,i==selected_slot?6:i==picked_slot?7:5);
        if(i==selected_slot)border(1,x+1,y+1,36,22,6);
    }
}
void theme_home(int selected){
    theme_saves(0,0);
    for(int i=0;i<6;i++){
        int y=home_y(i);
        rect(1,8,y,240,16,i==selected?4:3);
        border(1,8,y,240,16,i==selected?6:5);
        if(i==selected)rect(1,8,y,3,16,6);
    }
}
void theme_dialog(int screen){
    base(screen);rect(screen,8,32,240,128,3);border(screen,8,32,240,128,5);
}
