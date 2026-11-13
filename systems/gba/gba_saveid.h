#ifndef GBA_SAVEID_H
#define GBA_SAVEID_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define GBA_ID_LEN 24
/* Stable across paths and ZIP extraction. Versioned sampled or legacy full CRC. */
void gba_saveid_header(const unsigned char header[192], uint32_t crc, char id[GBA_ID_LEN]);
bool gba_saveid_resolve(const char *path, unsigned char header[192], uint32_t *crc, char id[GBA_ID_LEN]);
bool gba_saveid_path(const char *path, char id[GBA_ID_LEN]);
/* Copy legacy data only if absent. Originals and backups are never removed.
 * A durable owner claim prevents another cartridge adopting the same basename. */
bool gba_saveid_migrate(const char *path, const char *id);
bool gba_saveid_copy(const char *old, const char *next);
/* Timestamp sidecar is bound to the state payload CRC, so a failed replacement
 * cannot display the previous state's time. Old states report unknown (0). */
bool gba_state_time_write(const char *path, uint32_t crc, uint32_t saved);
uint32_t gba_state_time(const char *path);
#endif
