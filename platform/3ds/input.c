/* CTR sampling only. System adapters own interpretation of the snapshot. */
#include <string.h>
#include "app_input.h"
#include "input_transport.h"
#include <string.h>
static u32 transport_held, transport_down, transport_remote_key;
static input_contact_t transport_contact;
static void (*handler)(void);
void input_transport_handler(void (*poll)(void)) { handler = poll; }
u32 input_keys_down(void) { return transport_down; }
u32 input_keys_held(void) { return transport_held; }
input_contact_t input_contact(void) { return transport_contact; }
bool input_remote_key(const char *name)
{
 static const char *const names[] = {"a","b","x","y","up","down","left","right","l","r","select"};
 static const u32 keys[] = {KEY_A,KEY_B,KEY_X,KEY_Y,KEY_UP,KEY_DOWN,KEY_LEFT,KEY_RIGHT,KEY_L,KEY_R,KEY_SELECT};
 if (handler || transport_remote_key) return false;
 for (unsigned i=0; i<sizeof(keys)/sizeof(keys[0]); i++) if (!strcmp(name,names[i])) {
  transport_remote_key=keys[i]; return true;
 }
 return false;
}
void input_poll(void)
{
 hidScanInput(); transport_held=hidKeysHeld(); transport_down=hidKeysDown() | (handler ? 0 : transport_remote_key); transport_remote_key=0;
 touchPosition t; hidTouchRead(&t);
 transport_contact=(input_contact_t){t.px,t.py,(transport_held&KEY_TOUCH)!=0,(transport_down&KEY_TOUCH)!=0};
 if (handler) handler();
}
