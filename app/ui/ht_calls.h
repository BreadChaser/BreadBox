// Forced into frontend C/C++ compilations only in the optional diagnostic build.
// Do not interpose these names globally: that changes calls inside pinned cores.
#pragma once
#ifdef HT_TRACE
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <sys/stat.h>
#ifdef __cplusplus
extern "C" {
#endif
FILE *ht_fopen(const char *, const char *);
size_t ht_fwrite(const void *, size_t, size_t, FILE *);
int ht_stat(const char *, struct stat *);
void *ht_linearAlloc(size_t);
void *ht_vramAlloc(size_t);
#ifdef __cplusplus
}
#endif
#define fopen(...) ht_fopen(__VA_ARGS__)
#define fwrite(...) ht_fwrite(__VA_ARGS__)
#define stat(...) ht_stat(__VA_ARGS__)
#define linearAlloc(...) ht_linearAlloc(__VA_ARGS__)
#define vramAlloc(...) ht_vramAlloc(__VA_ARGS__)
#endif
