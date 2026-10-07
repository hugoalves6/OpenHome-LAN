#ifndef PORT_DRAFT_H
#define PORT_DRAFT_H
#include <stddef.h>
#define DRAFT_BOX_SIZE 7809
#define DRAFT_MAX_MOVES 128
typedef struct { unsigned char source_box, source_slot, target_box, target_slot; } DraftMove;
void draft_reset(void);
unsigned char *draft_box(int box, int (*fetch)(int, char *, size_t));
int draft_swap(int source_box, int source_slot, int target_box, int target_slot);
int draft_count(void);
const DraftMove *draft_moves(void);
void draft_sha(char out[65]);
#endif
