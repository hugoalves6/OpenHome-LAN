#include "mini_core.h"
#include "sha256.h"
#define JSMN_STATIC
#include "jsmn.h"
#include <stdio.h>
#include <string.h>

int mini_sha_file(const char *path, char output[65]) {
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    SHA256_CTX ctx; static unsigned char buffer[4096]; unsigned char digest[32]; size_t n, total = 0;
    sha256_init(&ctx);
    while ((n = fread(buffer, 1, sizeof(buffer), file))) {
        sha256_update(&ctx, buffer, n); total += n;
    }
    int failed = ferror(file) || !total;
    fclose(file);
    if (failed) return -1;
    sha256_final(&ctx, digest);
    for (int i = 0; i < 32; i++) sprintf(output + 2*i, "%02x", digest[i]);
    output[64] = 0; return 0;
}

/* 0 identical, 1 upload, 2 download, 3 conflict. No date-based guesses. */
int mini_action(const char *local, const char *hub, const char *baseline) {
    if (!strcmp(local, hub)) return 0;
    if (!*hub) return !*baseline ? 1 : 3;
    if (!*baseline) return 3;
    if (!strcmp(local, baseline)) return 2;
    if (!strcmp(hub, baseline)) return 1;
    return 3;
}

static int equal(const char *json, jsmntok_t token, const char *text) {
    return token.type == JSMN_STRING && token.end-token.start == (int)strlen(text)
        && !strncmp(json+token.start, text, token.end-token.start);
}
static int copy(const char *json, jsmntok_t token, char *out, size_t cap) {
    size_t n = token.end-token.start;
    if (token.type != JSMN_STRING || n >= cap) return -1;
    memcpy(out, json+token.start, n); out[n] = 0; return 0;
}
int mini_json_string(const char *json, const char *key, char *out, size_t cap) {
    jsmn_parser parser; jsmntok_t tokens[32]; jsmn_init(&parser);
    int count = jsmn_parse(&parser, json, strlen(json), tokens, 32);
    if (count < 1 || tokens[0].type != JSMN_OBJECT) return -1;
    for (int i=1; i+1<count; i++)
        if (equal(json, tokens[i], key)) return copy(json, tokens[i+1], out, cap);
    return -1;
}
int mini_listing_sha(const char *json, const char *name, char out[65]) {
    /* Single-threaded app: keep the 16 KiB token array off the ARM9 stack. */
    jsmn_parser parser; static jsmntok_t tokens[1024]; jsmn_init(&parser);
    int count = jsmn_parse(&parser, json, strlen(json), tokens, 1024);
    if (count < 1 || tokens[0].type != JSMN_OBJECT) return -1;
    out[0] = 0;
    for (int i=1; i<count; i++) {
        if (tokens[i].type != JSMN_OBJECT) continue;
        int end = tokens[i].end, matches = 0, sha_index = -1;
        for (int j=i+1; j+1<count && tokens[j].start<end; j++) {
            if (equal(json, tokens[j], "name") && equal(json, tokens[j+1], name)) matches=1;
            if (equal(json, tokens[j], "sha")) sha_index=j+1;
        }
        if (matches) {
            if (sha_index < 0 || copy(json, tokens[sha_index], out, 65) || strlen(out)!=64) return -1;
            for (int j=0; j<64; j++) if (!((out[j]>='0' && out[j]<='9') || (out[j]>='a' && out[j]<='f'))) return -1;
            return 1;
        }
    }
    return 0;
}
