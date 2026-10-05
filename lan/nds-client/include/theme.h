#ifndef THEME_H
#define THEME_H
#include <nds.h>
void theme_text(PrintConsole *console,int x,int y,int color,const char *text);
void theme_home_label(PrintConsole *console,int index,int color,const char *text);
int theme_home_hit(int x,int y);
void theme_init(PrintConsole *top,PrintConsole *bottom);
void theme_saves(int selected_row,int rows);
void theme_home(int selected);
void theme_boxes(int selected_slot,int picked_slot,int stats);
void theme_dialog(int screen);
#endif
