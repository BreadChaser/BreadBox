// The app log (log.c)
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// log.c
void log_init(void);
bool log_linked(void);
void log_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
bool log_close_timeout(uint64_t timeout);
void log_close(void);
void log_resume(void);
void log_hold(bool hold);
void log_soon(void);   // the lines so far to the card now, not waiting for it (BreadBox: its writer thread)
void log_writer_start(int core);   // BreadBox: the log's writer thread (main.c, once the model is known)
