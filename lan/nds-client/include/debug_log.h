#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H
void debug_init(const char *version);
void debug_log(const char *format,...);
void debug_upload_previous(const char *card_id);
#endif
