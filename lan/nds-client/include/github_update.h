#ifndef GITHUB_UPDATE_H
#define GITHUB_UPDATE_H
#include <stddef.h>
void github_update_seed(const char *hub_session);
int github_update_ready(void);
int github_update_info(char *out,size_t capacity);
int github_update_download(const char *url,const char *temporary,const char *sha256);
void github_update_progress(void (*callback)(const char *,size_t,size_t));
int github_update_newer(const char *available,const char *installed);
#endif
