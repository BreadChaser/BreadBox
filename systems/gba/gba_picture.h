/* GPL-2.0-or-later. Optional GBA picture presentation. */
#ifndef BB_GBA_PICTURE_H
#define BB_GBA_PICTURE_H
#include <stddef.h>
void gba_picture_begin(void);
void gba_picture_end(void);
void gba_picture_reset(void);
const char *gba_picture_error(void);
void gba_picture_video(const void *,unsigned,unsigned,size_t);
#endif
