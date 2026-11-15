#ifndef BB_DISC_H
#define BB_DISC_H
#include <stdbool.h>
/* Bounded content probe of a CHD (hunk zero and the ISO volume descriptor).
 * OTHER: readable and positively not a PS1 disc (another console's disc, a hard disk).
 * UNREAD: could not be opened or checked here; that proves nothing, so it stays listed. */
typedef enum { BB_DISC_OTHER, BB_DISC_UNREAD, BB_DISC_PS1 } bb_disc_kind;
bb_disc_kind bb_disc_chd_kind(const char *path);
#endif
