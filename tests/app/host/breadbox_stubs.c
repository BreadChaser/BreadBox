// BreadBox session and model hooks for host suites that link the shared frontend
// (config.c, options.c, ui.c) without the session layer or a system adapter.
#include <stdbool.h>
extern bool g_new3ds;
bool ps1_3d_available(void) { return g_new3ds; }
bool bb_session_active(void) { return false; }
/* Menu sounds (uisound.c): silent on the host. */
#include <stddef.h>
#include "uisound.h"
void sfx_play(int event) { (void)event; }
void sfx_default(int event) { (void)event; }
void sfx_now(int event) { (void)event; }
void sfx_cancel(void) {}
void sfx_flush(void) {}
void sfx_card_closes(void) {}
bool sfx_close_said(void) { return false; }
