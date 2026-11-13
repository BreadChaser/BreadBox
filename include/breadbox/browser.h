// The folder browser (browser.c)
#pragma once
#include <stdbool.h>
#include <stddef.h>

// browser.c - returns true and fills path when a game is chosen
bool browser_run(char *out_path, size_t out_len);
