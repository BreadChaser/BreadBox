// cards.c on the host: what libctru would declare
#include <stddef.h>
#include <stdint.h>
uint64_t osGetTime(void);
// the core's pad (cards_begin settles it: two toggles)
__attribute__((weak)) unsigned host_pad_toggles;
__attribute__((weak)) int padToggleAnalog(unsigned int index) { (void)index; return ++host_pad_toggles & 1; }
