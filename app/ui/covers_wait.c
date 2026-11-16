/* Menu transitions only; no GPU work while servicing APT. */
#include <3ds.h>
#include "covers.h"
#include "app_log.h"
#include "cold_code.h"

unsigned covers_stop_ms, covers_max_stop_ms;
static bool interrupted, cancelled, waiting;
static u64 wait_start;
static unsigned lifecycle_change;
static PSX3D_COLD u64 wait_now(void) {return svcGetSystemTick() / (SYSCLOCK_ARM11 / 1000);}

PSX3D_COLD const char *covers_wait_error(void)
{
 if (!interrupted) return NULL;
 return cancelled ? "Could not start the game\nStart canceled. Try again when ready." :
                    "Could not start the game\nStill busy. Try again in a moment.";
}

static PSX3D_COLD bool wait_for_stop(bool lifecycle)
{
 bool outer = !waiting;
 if (outer) {wait_start = wait_now(); waiting = true;}
 u64 start = wait_start;
 unsigned epoch = lifecycle_change;
 bool warned = false, ok = false;
 const char *pending = "background work";
 interrupted = cancelled = false;
 for (;;) {
  if (wait_now() - start >= COVERS_STOP_DEADLINE_MS) break;
  pending = covers_stop_pending();
  if (wait_now() - start >= COVERS_STOP_DEADLINE_MS) break;
  /* Already drained: do not dispatch APT hooks that could resume work between
   * a successful launch preflight and the prepared core entry. */
  if (!pending && (!lifecycle || (epoch == lifecycle_change &&
      !aptShouldClose() && gspHasGpuRight() && !covers_wait_home()))) {ok = true; break;}
  if (lifecycle) {
   if (!aptMainLoop() || aptShouldClose() || !gspHasGpuRight() || epoch != lifecycle_change) {
    pending = covers_stop_pending(); cancelled = true; break;
   }
   hidScanInput();
   if (hidKeysDown() & KEY_B) {cancelled = true; break;}
   if (covers_wait_home()) {
    cancelled = true;
    /* A live SOC consumer must not be carried into the HOME Menu. */
    if (!pending && covers_home_frame()) break;
   }
  }
  if (!warned && wait_now() - start >= 5000) {
   log_printf("covers: waiting over 5s for %s\n", pending ? pending : "lifecycle");
   warned = true;
  }
  svcSleepThread(10000000LL);
 }
 covers_stop_ms = wait_now() - start;
 if (covers_stop_ms > covers_max_stop_ms) covers_max_stop_ms = covers_stop_ms;
 if (!ok) {
  interrupted = true;
  log_printf("covers: stopped waiting for %s after %u ms (deadline or cancellation)\n", pending ? pending : "lifecycle", covers_stop_ms);
 }
 if (outer) waiting = false;
 return ok;
}

PSX3D_COLD bool covers_stop_wait(void) { return wait_for_stop(true); }
PSX3D_COLD bool covers_stop_wait_apt(void) { lifecycle_change++; return wait_for_stop(false); }
