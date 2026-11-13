#ifndef BREADBOX_GBA_CART_H
#define BREADBOX_GBA_CART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GBA_CART_MAX_BYTES (32u * 1024u * 1024u)
#define GBA_CART_CACHE_DIR "sdmc:/3ds/BreadBox/cache/gba"
#define GBA_CART_CACHE_MAX_BYTES GBA_CART_MAX_BYTES

typedef enum {
    GBA_CART_OK = 0,
    GBA_CART_INVALID,
    GBA_CART_IO_ERROR,
    GBA_CART_MULTI_ROM,
    GBA_CART_TOO_LARGE,
    GBA_CART_UNSUPPORTED,
    GBA_CART_CORRUPT
} gba_cart_result;

bool gba_cart_is_path(const char *path);
/* Bounded content samples; existing full-CRC identity records are retained. */
bool gba_cart_fingerprint(const char *path, unsigned char header[192], uint32_t *crc);
/* Explicit legacy verification only; never call during library enumeration. */
bool gba_cart_index_fingerprint(const char *path, unsigned char header[192], uint32_t *crc);
bool gba_cart_identity(const char *path, unsigned char header[192], uint32_t *crc, bool *legacy, uint32_t *sample);
bool gba_cart_header(const char *path, unsigned char header[192]);
/* ZIPs are fully streamed and CRC checked. Direct paths are returned unchanged. */
gba_cart_result gba_cart_prepare(const char *path, const char **rom_path);
gba_cart_result gba_cart_prepare_progress(const char *path, const char **rom_path, bool (*progress)(unsigned bytes, unsigned total));
/* Over GBA_CART_MAX_BYTES (a .gba, or the one game in a .zip): the reason the
 * player is told when such a file cannot be identified or started. */
bool gba_cart_too_large(const char *path);
const char *gba_cart_error(gba_cart_result result);
/* gpSP's save type after loading a cartridge (gba_memory.h BACKUP_*, FLASH_SIZE_*). */
enum { GBA_SAVE_SRAM, GBA_SAVE_FLASH, GBA_SAVE_EEPROM, GBA_SAVE_UNKNOWN };
enum { GBA_FLASH_64K = 1, GBA_FLASH_128K = 2 };
/* A save file of this many bytes could be the cartridge's own chip as other emulators write it:
 * EEPROM 512 B or 8 KiB, SRAM 32 KiB, 64K flash 64 KiB; any of those when the type is unknown;
 * none for 128K flash. Only adds "Use it" to the damaged-save question (bb_save_ops.battery_native);
 * the size alone never proves where the file came from or the EEPROM's capacity. */
static inline bool gba_cart_save_fits(uint32_t type, uint32_t flash_banks, size_t bytes)
{
    bool eeprom = bytes == 512 || bytes == 8192, sram = bytes == 32768, flash = bytes == 65536;
    if (type == GBA_SAVE_EEPROM) return eeprom;
    if (type == GBA_SAVE_SRAM) return sram;
    if (flash_banks == GBA_FLASH_128K) return false;
    if (type == GBA_SAVE_FLASH) return flash;
    return eeprom || sram || flash;
}

#endif
