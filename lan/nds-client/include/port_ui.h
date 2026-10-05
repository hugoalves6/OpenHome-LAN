#ifndef PORT_UI_H
#define PORT_UI_H
#include <nds.h>
void port_init(void);
void port_browser(PrintConsole *top, PrintConsole *bottom, const char *remote);
#endif
