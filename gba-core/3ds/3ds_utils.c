/* BreadBox: use Luma cache SVCs, never svcBackdoor. */
#include "3ds_utils.h"
void check_rosalina(void) {}
void ctr_flush_invalidate_cache(void) { ctr_clear_cache(); }
void ctr_flush_DCache(void) { ctr_clear_cache(); }
void ctr_invalidate_ICache(void) { ctr_clear_cache(); }
