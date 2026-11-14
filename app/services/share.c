#include "app_policy.h"
// Settings > About (experimental): share PSX3D itself. The bottom screen shows
// a QR code for FBI's "Scan QR code": the official release CIA, or, while
// "Share from this 3DS" is on, this console serving its own copy of the
// PSX3D CIA over the local Wi-Fi (HTTP, LAN only). Only that one file is ever
// served, and only after AM confirms it is a PSX3D CIA: never games or saves.
#include <string.h>
#include "theme.h"
#include "app_paths.h"
#include "app_log.h"
#include "platform_video.h"
#include "app_input.h"
#include "ui_list.h"
#include "ui_pause.h"
#include "ui.h"
#include "share_about.h"
#include "app_main.h"
#include "scene.h"
#include "devtools.h"
#include "qrcode.h"
#include "share.h"
#include "ui_motion.h"
#include "breadbox_credits.h"
#include "title_version.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define RELEASE_URL APP_RELEASE_URL
#define SELF_TITLE APP_TITLE_ID
#define SHARE_PORT 8080

// A kept copy first (a future self-update can leave its download here), then
// the folders people usually put a CIA in before installing it with FBI.
static const char *const cia_paths[] = {
	PSX3D_DIR "/" APP_NAME ".cia", "sdmc:/" APP_NAME ".cia", "sdmc:/cias/" APP_NAME ".cia", "sdmc:/cia/" APP_NAME ".cia",
	"sdmc:/CIA/" APP_NAME ".cia", "sdmc:/3ds/" APP_NAME ".cia", "sdmc:/Downloads/" APP_NAME ".cia"};

static char cia_path[64], cia_note[160], url[64];
static u64 cia_size;
static volatile bool serving, server_stop;
static volatile u32 served_count;
static Thread server_thread;
static int listen_fd = -1;

// Is the file the build running here? An installed title compares its own
// counter (a test build's too); a 3DSX can only compare release text.
static bool same_as_running(u16 file_version, const char *file_text)
{
	u64 self = SELF_TITLE;
	AM_TitleInfo running;
	if (!envIsHomebrew() && R_SUCCEEDED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &self, &running)))
		return running.version == file_version;
	return !strcmp(file_text, BB_VERSION);
}

// AM reads the CIA's own title ID and version: only a real PSX3D CIA is served.
static bool find_cia(void)
{
	cia_path[0] = 0;
	snprintf(cia_note, sizeof(cia_note), "" APP_NAME ".cia wasn't found. Copy it to sdmc:/cias/ and try again.");
	if (R_FAILED(amInit()))
		return false;
	for (unsigned i = 0; i < sizeof(cia_paths) / sizeof(cia_paths[0]) && !cia_path[0]; i++) {
		Handle h;
		if (R_FAILED(FSUSER_OpenFileDirectly(&h, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, ""),
		                                     fsMakePath(PATH_ASCII, cia_paths[i] + 5), FS_OPEN_READ, 0)))
			continue;
		snprintf(cia_note, sizeof(cia_note), "Invalid or mismatched " APP_NAME ".cia on the SD card");
		AM_TitleEntry info;
		u64 bytes = 0;
		if (R_SUCCEEDED(AM_GetCiaFileInfo(MEDIATYPE_SD, &info, h)) && info.titleID == SELF_TITLE &&
		    R_SUCCEEDED(FSFILE_GetSize(h, &bytes)) && bytes) {
			snprintf(cia_path, sizeof(cia_path), "%s", cia_paths[i]);
			cia_size = bytes;
			char version[24];
			bb_title_version_text(version, sizeof(version), info.version);
			snprintf(cia_note, sizeof(cia_note), "" APP_NAME " %s%s", version,
			         same_as_running(info.version, version) ? "" : ". This file differs from the version running here");
		}
		FSFILE_Close(h);
	}
	amExit();
	return cia_path[0] != 0;
}

static bool lan_ok(const struct sockaddr_in *a, u32 *self_ip)
{
	SOCU_IPInfo ip;
	socklen_t n = sizeof(ip);
	if (SOCU_GetNetworkOpt(SOL_CONFIG, NETOPT_IP_INFO, &ip, &n))
		return false;
	if (self_ip)
		*self_ip = ip.ip.s_addr;
	return !a || (a->sin_addr.s_addr & ip.netmask.s_addr) == (ip.ip.s_addr & ip.netmask.s_addr);
}

static bool send_all(int fd, const void *b, size_t n)
{
	const u8 *p = b;
	u64 deadline = osGetTime() + 20000;
	while (n && !server_stop) {
		int r = send(fd, p, n > 16384 ? 16384 : n, 0);
		if (r > 0) {
			p += r;
			n -= r;
			deadline = osGetTime() + 20000;
		} else if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
			return false;
		else if (osGetTime() > deadline)
			return false;
		else
			svcSleepThread(1000000);
	}
	return !n;
}

static void serve(int fd)
{
	char req[1024];
	size_t used = 0;
	req[0] = 0;
	u64 deadline = osGetTime() + 10000;
	while (used < sizeof(req) - 1 && !strstr(req, "\r\n\r\n") && osGetTime() < deadline && !server_stop) {
		int r = recv(fd, req + used, sizeof(req) - 1 - used, 0);
		if (r > 0) {
			used += r;
			req[used] = 0;
		} else if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
			return;
		else
			svcSleepThread(2000000);
	}
	bool head = !strncmp(req, "HEAD ", 5);
	bool ok = (head || !strncmp(req, "GET ", 4)) && !strncmp(req + (head ? 5 : 4), "/" APP_CIA " ", sizeof("/" APP_CIA " ") - 1);
	FILE *f = ok ? fopen(cia_path, "rb") : NULL;
	if (!f) {
		static const char nf[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
		send_all(fd, nf, sizeof(nf) - 1);
		return;
	}
	char hdr[256];
	int n = snprintf(hdr, sizeof(hdr),
	                 "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %llu\r\n"
	                 "Content-Disposition: attachment; filename=\"" APP_NAME ".cia\"\r\nConnection: close\r\n\r\n",
	                 (unsigned long long)cia_size);
	bool good = send_all(fd, hdr, n);
	static u8 buf[32768];
	u64 left = head ? 0 : cia_size;
	while (good && left) {
		size_t chunk = left > sizeof(buf) ? sizeof(buf) : (size_t)left;
		good = fread(buf, 1, chunk, f) == chunk && send_all(fd, buf, chunk);
		left -= chunk;
	}
	fclose(f);
	if (good && !head)
		served_count++;
	log_printf("share: %s %s\n", head ? "HEAD" : "GET", good ? "sent" : "failed");
}

static void server_main(void *unused)
{
	(void)unused;
	while (!server_stop) {
		struct sockaddr_in peer;
		socklen_t n = sizeof(peer);
		int fd = accept(listen_fd, (struct sockaddr *)&peer, &n);
		if (fd >= 0) {
			fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
			if (lan_ok(&peer, NULL))
				serve(fd);
			shutdown(fd, SHUT_WR);
			u64 end = osGetTime() + 1000;
			char discard[64];
			while (osGetTime() < end) {   // let the reader drain, then close
				int r = recv(fd, discard, sizeof(discard), 0);
				if (!r || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK))
					break;
				svcSleepThread(2000000);
			}
			close(fd);
		}
		svcSleepThread(50000000);
	}
}

static u32 *soc_own;
static bool share_start(void)
{
	if (!find_cia())
		return false;
	int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s < 0 && !soc_own) {
		soc_own = memalign(0x1000, 0x40000);
		if (soc_own && R_FAILED(socInit(soc_own, 0x40000))) {
			free(soc_own);
			soc_own = NULL;
		}
		s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	}
	u32 ip = 0;
	if (s < 0 || !lan_ok(NULL, &ip) || !ip) {
		if (s >= 0)
			close(s);
		snprintf(cia_note, sizeof(cia_note), "No Wi-Fi connection");
		return false;
	}
	fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
	int yes = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
	struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(SHARE_PORT), .sin_addr = {INADDR_ANY}};
	if (bind(s, (struct sockaddr *)&a, sizeof(a)) || listen(s, 2)) {
		close(s);
		snprintf(cia_note, sizeof(cia_note), "Port %d is busy", SHARE_PORT);
		return false;
	}
	listen_fd = s;
	const u8 *b = (const u8 *)&ip;   // network order: the address's bytes in order
	snprintf(url, sizeof(url), "http://%u.%u.%u.%u:%d/" APP_NAME ".cia", b[0], b[1], b[2], b[3], SHARE_PORT);
	server_stop = false;
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	server_thread = threadCreate(server_main, NULL, 16 * 1024, prio + 4, g_syscore_pct > 0 ? 1 : 0, false);
	if (!server_thread) {
		close(s);
		listen_fd = -1;
		snprintf(cia_note, sizeof(cia_note), "Could not start sharing. Try again.");
		return false;
	}
	serving = true;
	log_printf("share: serving %s (%llu bytes) at %s\n", cia_path, (unsigned long long)cia_size, url);
	return true;
}

static void share_stop(void)
{
	if (!serving)
		return;
	server_stop = true;
	threadJoin(server_thread, U64_MAX);
	threadFree(server_thread);
	server_thread = NULL;
	close(listen_fd);
	listen_fd = -1;
	serving = false;
}

static uint8_t qr[QR_MAX_SIZE * QR_MAX_SIZE];
static int qr_size;
static char qr_text[96];
static bool in_game, share_failed;
static const ui_destination *share_scene;
static bool share_leaving;
static float share_age, share_card, share_exit_card;

static bool scene_settled(void)
{
 return !share_scene || !share_scene->settled || share_scene->settled(share_scene->userdata);
}
static void share_leave(void)
{
 if (share_leaving) return;
 share_leaving = true;
 share_exit_card = share_card;
 share_age = 0;
 if (share_scene && share_scene->leave) share_scene->leave(share_scene->userdata);
}


static void draw_top(void *ud); /* rendered by the shared share component */

static void draw_bottom(void *ud); /* rendered by the shared share component */

static void make_qr(const char *text)
{
	if (strcmp(qr_text, text)) {
		snprintf(qr_text, sizeof(qr_text), "%s", text);
		if (!qr_encode(qr_text, qr, &qr_size))
			qr_size = 0;
	}
}

// Present the share entries in a scrollable list:
// up / down or a touch moves through it, the top screen tells about the one in focus, A acts
// on it (a QR code: shown full size below, any key back)
enum { SHARE_RELEASE, SHARE_LOCAL, SHARE_INSTALL, SHARE_CREDITS };
static int share_items[4], share_n, share_sel;
static bool share_qr, share_start_pending;
static flist_t share_list;
static const char *share_label(int i, void *ud)
{
	(void)ud;
	switch (share_items[i]) {
	case SHARE_RELEASE: return "Official release";
	case SHARE_LOCAL: return "From this 3DS";
	case SHARE_INSTALL: return "How to install";
	}
	return "Credits";
}
static const char *share_value(int i, void *ud)
{
	(void)ud;
	switch (share_items[i]) {
	case SHARE_RELEASE: return "QR code";
	case SHARE_LOCAL: return serving ? "Sharing" : "Off";
	}
	return NULL;
}
static int share_item(void) { return share_n ? share_items[share_sel] : SHARE_RELEASE; }
// Runs until B. Sharing is offered in the library only (no SD reads that
// could compete with a running game's disc).
void share_about_run(bool game)
{
	in_game = game;
	share_failed = false;
	cia_note[0] = 0;
	share_n = 0;
	share_items[share_n++] = SHARE_RELEASE;
	if (!game) share_items[share_n++] = SHARE_LOCAL;
	share_items[share_n++] = SHARE_INSTALL;
	share_items[share_n++] = SHARE_CREDITS;
	share_sel = 0;
	share_qr = share_start_pending = false;
	share_list.init = false;
 share_leaving = false;
 share_age = share_card = 0;
 if (share_scene && share_scene->enter) share_scene->enter(share_scene->userdata);
 const ui_destination *previous_scene = ui_dialog_scene_set(share_scene);
 u64 last_tick = svcGetSystemTick();
 ui_swap_enter();
 while (aptMainLoop()) {
  if (dev_tick(game) || dev_ui_pending()) break;
  u64 now = svcGetSystemTick();
  float dt = (float)(now - last_tick) / SYSCLOCK_ARM11;
  last_tick = now;
  share_age += dt;
  if (share_scene && share_scene->tick) share_scene->tick(share_scene->userdata, dt);
  share_card = share_leaving ? fmaxf(0, share_exit_card - share_age / UI_MOTION_UI_S) :
      fminf(1, fmaxf(0, (share_age - (share_scene ? UI_MOTION_CAMERA_S - UI_MOTION_UI_S : 0)) / UI_MOTION_UI_S));
  if (share_leaving && scene_settled()) break;
  if (share_start_pending) {
   share_start_pending = false;
   share_qr = !(share_failed = !share_start());
   if (share_qr) make_qr(url);
   ui_swap_enter();
   last_tick = svcGetSystemTick();
  }
  input_poll();
  u32 k = input_keys_down();
  if (share_leaving) k = 0;
  else if (!scene_settled()) {
   // B reverses immediately from the camera's actual pose. A never queues a launch.
   if (k & KEY_B) share_leave();
   k = 0;
  } else if (share_qr) {
   if (k & KEY_TOUCH) k |= KEY_A;
   k = ui_swap_keys(k, UI_SWAP_MENU);
   if (k & (KEY_A | KEY_B)) { share_qr = false; ui_swap_enter(); }
   if ((k & KEY_X) && serving) { share_stop(); share_qr = false; ui_swap_enter(); }
  } else {
   k = ui_card_input(&share_list, share_n, &share_sel, k);
   k = ui_swap_keys(k, UI_SWAP_MENU);
   if (k & KEY_B) { if (serving) server_stop = true; share_leave(); }
   else if (k & KEY_A) {
    int it = share_item();
    if (it == SHARE_RELEASE) share_qr = true;
    else if (it == SHARE_LOCAL && serving) share_qr = true;
    else if (it == SHARE_LOCAL) share_start_pending = true;
    else if (it == SHARE_INSTALL || it == SHARE_CREDITS) {
     if (it == SHARE_INSTALL) bb_install_help_run();
     else bb_credits_run();
     if (!aptMainLoop()) break;   // HOME > Close: no GPU work after the dialog
     ui_swap_enter();
     last_tick = svcGetSystemTick();
    }
    if (share_qr) { make_qr(it == SHARE_LOCAL ? url : RELEASE_URL); ui_swap_enter(); }
   }
   if ((k & KEY_X) && serving) share_stop();
  }
  if (!aptMainLoop()) break;   // a suspend/close during input never submits another frame
  ui_card_update(&share_list, share_sel, share_n);
  ui_pause_motion(share_card);
  ui_begin();
  video_present_vsync(game, draw_top, draw_bottom, NULL);
 }
 ui_dialog_scene_set(previous_scene);
 ui_pause_motion(1);
	share_stop();
	return;
	in_game = game;
	share_failed = false;
	cia_note[0] = 0;
	while (aptMainLoop()) {
		if (dev_tick(game) || dev_ui_pending())
			break;
		input_poll();
		u32 k = input_keys_down();
		if (k & KEY_B)
			break;
		if ((k & KEY_X) && !game) {
			if (serving)
				share_stop();
			else
				share_failed = !share_start();
		}
		make_qr(serving ? url : RELEASE_URL);
		ui_begin();
		video_present_vsync(game, draw_top, draw_bottom, NULL);
	}
	share_stop();
}

void share_destination_run(const ui_destination *scene)
{
 const ui_destination *previous = share_scene;
 share_scene = scene;
 share_about_run(false);
 share_scene = previous;
}

// Prototype: only on consoles with the developer remote switched on.
bool share_about_visible(void) { return dev_enabled(); }

#include "components/share.inc"
