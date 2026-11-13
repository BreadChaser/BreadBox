/* Host stand-in for newlib's <reent.h>: the pool only needs the lock argument. */
struct _reent;
#define _REENT ((struct _reent *)0)
