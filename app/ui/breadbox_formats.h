#ifndef BREADBOX_FORMATS_H
#define BREADBOX_FORMATS_H
#include <stdbool.h>
#include <stddef.h>
/* breadbox_formats.c: the registered systems' names and file types. */
bool bb_formats_match(const char *name);               /* some system lists this file name */
void bb_formats_systems(char *out,size_t cap);         /* display names, menu order, comma separated */
void bb_formats_extensions(char *out,size_t cap);      /* file types, each once, space separated */
#endif
