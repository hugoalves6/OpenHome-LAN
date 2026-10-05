#ifndef MINI_CORE_H
#define MINI_CORE_H
#include <stddef.h>
int mini_sha_file(const char *path, char output[65]);
int mini_action(const char *local, const char *hub, const char *baseline);
int mini_json_string(const char *json, const char *key, char *out, size_t cap);
int mini_listing_sha(const char *json, const char *name, char out[65]);
#endif
