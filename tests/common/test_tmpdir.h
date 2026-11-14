// Host tests: make a fresh directory under $TMPDIR (tools/build/host-tests.sh gives
// each group its own), or /tmp when TMPDIR is unset. Returns buf, or NULL.
#ifndef TEST_TMPDIR_H
#define TEST_TMPDIR_H
#include <stdio.h>
#include <stdlib.h>
static inline const char *test_tmp_base(void)
{
	const char *base = getenv("TMPDIR");
	return base && *base ? base : "/tmp";
}
static inline char *test_mkdtemp(char *buf, size_t size, const char *stem)
{
	if ((size_t)snprintf(buf, size, "%s/%s-XXXXXX", test_tmp_base(), stem) >= size)
		return NULL;
	return mkdtemp(buf);
}
#endif
