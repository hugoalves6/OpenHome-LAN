#include "port_draft.h"
#include <string.h>
#include <stdio.h>
static unsigned char boxes[24][DRAFT_BOX_SIZE], loaded[24], base_sha[32];
static DraftMove moves[DRAFT_MAX_MOVES];
static int count, initialized;
void draft_reset(void) { memset(loaded,0,sizeof(loaded)); count=initialized=0; }
unsigned char *draft_box(int box, int (*fetch)(int,char *,size_t)) {
    if(box<0 || box>=24)return NULL;
    if(!loaded[box]) {
        if(fetch(box,(char*)boxes[box],DRAFT_BOX_SIZE))return NULL;
        if(initialized && memcmp(base_sha,boxes[box]+8,32))return NULL;
        if(!initialized) { memcpy(base_sha,boxes[box]+8,32); initialized=1; }
        loaded[box]=1;
    }
    return boxes[box];
}
int draft_swap(int sb,int ss,int tb,int ts) {
    if(count>=DRAFT_MAX_MOVES || sb<0 || sb>=24 || tb<0 || tb>=24 || ss<0 || ss>=30 || ts<0 || ts>=30 ||
       !loaded[sb] || !loaded[tb] || (sb==tb && ss==ts))return -1;
    unsigned char *a=boxes[sb]+128+ss*256,*b=boxes[tb]+128+ts*256,temp[256];
    if(a[90]!=1 || b[90]==2)return -1;
    memcpy(temp,a,256);memcpy(a,b,256);memcpy(b,temp,256);
    moves[count++]=(DraftMove){sb,ss,tb,ts};return 0;
}
int draft_count(void) { return count; }
const DraftMove *draft_moves(void) { return moves; }
void draft_sha(char out[65]) { for(int i=0;i<32;i++)sprintf(out+i*2,"%02x",base_sha[i]);out[64]=0; }
