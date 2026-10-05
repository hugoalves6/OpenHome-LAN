#ifndef SETTINGS_UI_H
#define SETTINGS_UI_H
#include <nds.h>
int settings_open(PrintConsole *top,PrintConsole *bottom,const char *version);
void settings_diagnostics(PrintConsole *top,PrintConsole *bottom,const char *version);
#endif
