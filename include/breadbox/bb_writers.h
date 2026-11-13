#ifndef BB_WRITERS_H
#define BB_WRITERS_H
#include <stdbool.h>
/* Reserve before scheduling a writer; release after its final close/rename.
 * False means defer the operation: an installer has exclusive ownership.
 * Nested reservations are allowed. Never hold a reservation while waiting for
 * the installer. Import uses this neutral API, without depending on update.h. */
bool bb_writer_begin(void);
void bb_writer_end(void);
bool bb_writers_busy(void);
bool bb_writers_try_exclusive(void);
void bb_writers_release_exclusive(void);
bool bb_writers_exclusive(void);
#endif
