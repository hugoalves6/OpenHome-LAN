#ifndef MINI_NET_H
#define MINI_NET_H
#include <stdio.h>
#include <stddef.h>
extern char hub_host[64], hub_password[128], hub_token[128];
extern int hub_port;
extern char net_error[96];
int net_box(const char *remote, int box, char *out, size_t capacity);
int net_swap(const char *remote, const char *sha, int source_box, int source_slot, int target_box, int target_slot);
void net_set_progress(void (*callback)(const char *, size_t, size_t));
int net_login(void);
int net_live_pulse(const char *body);
int net_listing(char *out, size_t cap);
int net_update_info(char *out, size_t cap);
int net_upload(const char *local, const char *remote, const char *previous, const char *hash);
int net_download(const char *remote, const char *temporary, const char *expected);
#endif
