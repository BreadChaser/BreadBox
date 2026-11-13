#ifndef BB_SD_BENCH_H
#define BB_SD_BENCH_H
#include <stdbool.h>
/* Developer-only remote diagnostic. Runs on LOW, owns only its exclusive temp
 * file, reserves the writer through final cleanup, never touches player data. */
bool bb_sd_bench_start(void);
#endif
