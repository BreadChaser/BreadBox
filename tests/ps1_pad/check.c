// The PS1 pad as a game reads it: the real core pad code (core/libpcsxcore/pad.c)
// answers serial bytes sent the way the PlayStation pad library sends them,
// with ps1_pad_settle() called where the app calls it (game start, reset,
// state load). Checks the ID byte (0x41 digital, 0x73 analog) a game reads.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "psxcommon.h"
#include "plugins.h"
#include "psxcounters.h"
#include "ps1_pad.h"

PcsxConfig Config;
static int controller_setting;
int config_controller(void) { return controller_setting; }
void core_reset(void);   // reset boundary followed by the BIOS-initialized callback
void options_mark_updated(void) {}
void log_printf(const char *fmt, ...) { (void)fmt; }
unsigned int hSyncCount, frame_counter;
GPUgetScreenInfo GPU_getScreenInfo;
void pl_gun_byte2(int port, unsigned char byte) { (void)port; (void)byte; }
static int vibrations;
void plat_trigger_vibrate(int pad, int low, int high) { (void)pad; (void)low; (void)high; vibrations++; }
void SysPrintf(const char *fmt, ...) { (void)fmt; }

// The DualShock in port 1, idle Standard pad in port 2 (as emu.c plugs them in).
void PAD1_readPort(PadDataS *pad, int *is_multitap)
{
	pad->controllerType = pad->requestPadIndex == 0 ? PSE_PAD_TYPE_ANALOGPAD : PSE_PAD_TYPE_NONE;
	pad->buttonStatus = 0xffff;
	pad->leftJoyX = pad->leftJoyY = pad->rightJoyX = pad->rightJoyY = 0x80;
	if (is_multitap) *is_multitap = 0;
}
void PAD2_readPort(PadDataS *pad, int *is_multitap)
{
	pad->controllerType = PSE_PAD_TYPE_STANDARD;
	pad->buttonStatus = 0xffff;
	if (is_multitap) *is_multitap = 0;
}

// In-memory state file for padFreeze.
static unsigned char state[8 * sizeof(PadDataS)];
static size_t state_pos;
static int st_read(void *f, void *b, u32 n) { (void)f; memcpy(b, state + state_pos, n); state_pos += n; return n; }
static int st_write(void *f, const void *b, u32 n) { (void)f; memcpy(state + state_pos, b, n); state_pos += n; return n; }
static long st_seek(void *f, long o, int w) { (void)f; (void)w; state_pos += o; return 0; }
struct PcsxSaveFuncs SaveFuncs = { .read = st_read, .write = st_write, .seek = st_seek };

// One transfer on port 1: the bytes after the 0x01 address byte. Returns the
// first reply byte (the ID: 0x41, 0x73, or 0xF3 in config mode).
static unsigned char transfer(const unsigned char *tx, int n, unsigned char *rx)
{
	int more = 1;
	unsigned char id = 0;
	PAD1_startPoll();
	for (int i = 0; i < n && more; i++) {
		unsigned char r = PAD1_poll(tx[i], &more);
		if (rx) rx[i] = r;
		if (i == 0) id = r;
	}
	return id;
}
static unsigned char poll_id(void)
{
	static const unsigned char read[] = {0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	frame_counter++;
	return transfer(read, sizeof(read), NULL);
}
static void config(int on)
{
	unsigned char cmd[8] = {0x43, 0x00, (unsigned char)on};
	transfer(cmd, sizeof(cmd), NULL);
}
// What the pad library does at start: enter config mode, ask the model and
// mode (0x45), optionally set the mode (0x44 mode lock=3), leave config mode.
static void library_probe(int set_mode)
{
	static const unsigned char query[8] = {0x45};
	unsigned char rx[8];
	config(1);
	assert(transfer(query, sizeof(query), rx) == 0xf3);
	assert(rx[2] == 0x01);   // model: DualShock
	if (set_mode >= 0) {
		unsigned char mode[8] = {0x44, 0x00, (unsigned char)set_mode, 0x03};
		transfer(mode, sizeof(mode), NULL);
	}
	config(0);
}
static void plug_in(void)   // what the core does when emu.c plugs the pad in
{
	padChanged();
	frame_counter += 40;   // past the replug pause
	poll_id();             // its last unanswered transfer
}
static void expect_ids(unsigned char want, int polls, const char *what)
{
	for (int i = 0; i < polls; i++) {
		unsigned char id = poll_id();
		if (id != want) {
			fprintf(stderr, "FAIL %s: poll %d read ID %02x, want %02x\n", what, i, id, want);
			assert(0);
		}
	}
}
static void save_state(void) { state_pos = 0; padFreeze(NULL, 1); }
static void load_state(void) { state_pos = 0; padFreeze(NULL, 0); }

// emu.c: plug in, apply Controller, cards_begin() settles, then game frames.
static void game_start(int controller_analog)
{
	controller_setting = controller_analog;
	padChanged();
	if (controller_analog && !padAnalogMode(0))
		padToggleAnalog(0);
	ps1_pad_settle();
	frame_counter += 40;
	poll_id();   // finish the replug pause only after startup settlement
}

// The menu only requests a reset. On the next frame the core resets the pad
// and reports BIOS initialization before BIOS/game execution.
static void deferred_reset(unsigned recovery, const char *path, int analog)
{
	game_start(analog);
	library_probe(1);
	if (recovery == 1 || recovery == 3) {
		// The pad record can already be restored when a later state record
		// fails. A rewind snapshot can also be inside a config transaction.
		if (recovery == 3)
			config(1);
		save_state();
		padToggleAnalog(0);
		load_state();
		assert(padAnalogMode(0));
	}
	if (recovery == 2)
		padToggleAnalog(0);   // card restoration may restart from digital too
	ps1_pad_settle();   // any settlement before the pending reset is erased
	core_reset();      // next retro_run -> SysReset -> EmuReset -> BIOS callback
	library_probe(-1);
	expect_ids(analog ? 0x73 : 0x41, 600, path);
}

static void reset_paths(void)
{
	static const char *paths[] = {
		"Reset game, rewind off", "partly restored state",
		"memory-card restoration", "partly restored rewind"
	};
	for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
		for (int analog = 0; analog < 2; analog++)
			for (int hle = 0; hle < 2; hle++) {
				Config.HLE = hle;
				deferred_reset(i, paths[i], analog);
			}
}

// The saved pad cannot distinguish auto-enable from a later game choice.
// Prove that both histories can produce exactly the same serialized bytes.
static void legacy_mode_history(void)
{
	unsigned char automatic[sizeof(state)];
	plug_in();
	library_probe(-1);
	expect_ids(0x41, 17, "legacy auto-enable");
	assert(poll_id() == 0x73);
	expect_ids(0x73, 2, "legacy analog reads");
	save_state();
	memcpy(automatic, state, sizeof(state));
	PadDataS saved;
	memcpy(&saved, state, sizeof(saved));
	assert(saved.ds.autoAnalogTried == 255 && !saved.ds.userToggled);
	library_probe(0);   // the game explicitly changes its mind after auto-enable
	library_probe(1);
	// Several transfers can occur in one frame; overwrite the command buffers
	// with ordinary reads, keeping the saved frame timestamp identical.
	static const unsigned char read[8] = {0x42};
	for (int i = 0; i < 2; i++)
		assert(transfer(read, sizeof(read), NULL) == 0x73);
	save_state();
	assert(memcmp(automatic, state, sizeof(state)) == 0);
	controller_setting = 0;
	load_state(); ps1_pad_settle();
	expect_ids(0x73, 300, "ambiguous legacy analog preserved with Digital setting");
}

int main(void)
{
	frame_counter = 100;

	// The core on its own: after a config-mode probe that leaves the pad
	// digital it switches to analog after 16 reads. This is what Tomb Raider
	// met: it probes the DualShock at start, keeps it digital and cannot read
	// the 0x73 replies that follow.
	plug_in();
	library_probe(-1);
	expect_ids(0x41, 17, "core alone, first reads");
	assert(poll_id() == 0x73);

	// A game that only reads the pad (Crash Bandicoot).
	game_start(0);
	expect_ids(0x41, 600, "read only");

	// A game that probes and keeps it digital: 0x41, never a 0x73 it did not ask for.
	game_start(0);
	library_probe(-1);
	expect_ids(0x41, 600, "probed, left digital");
	assert(!padAnalogMode(0));
	game_start(0);
	library_probe(0);
	expect_ids(0x41, 600, "probed, set digital");

	// Analog-only games (Ape Escape) switch it to analog themselves.
	game_start(0);
	library_probe(1);
	expect_ids(0x73, 600, "game set analog");
	assert(padAnalogMode(0));
	{   // rumble: 0x4D maps the motors, then reads drive them
		static const unsigned char map[8] = {0x4d, 0x00, 0x00, 0x01, 0xff, 0xff, 0xff, 0xff};
		static const unsigned char buzz[8] = {0x42, 0x00, 0xff, 0xff};
		config(1); transfer(map, sizeof(map), NULL); config(0);
		vibrations = 0;
		transfer(buzz, sizeof(buzz), NULL);
		assert(vibrations == 1);
	}

	// Controller: Analog (setting or a per-game preset) at start.
	game_start(1);
	library_probe(-1);
	expect_ids(0x73, 300, "Controller: Analog");
	// Back to Digital (the setting row, or the analog shortcut).
	padToggleAnalog(0);
	expect_ids(0x41, 300, "Controller back to Digital");
	padToggleAnalog(0);
	expect_ids(0x73, 300, "shortcut to analog");
	padToggleAnalog(0);
	expect_ids(0x41, 300, "shortcut to digital");

	reset_paths();
	legacy_mode_history();

	// State loads (save states, rewind, Continue) keep the mode.
	game_start(0);
	library_probe(1);
	save_state();
	game_start(0);
	load_state(); ps1_pad_settle();
	expect_ids(0x73, 300, "game analog after load");

	game_start(0);
	library_probe(-1);
	padToggleAnalog(0);   // the player chose analog
	save_state();
	game_start(0);
	load_state(); ps1_pad_settle();
	expect_ids(0x73, 300, "player analog after load");

	// A state from an older build, saved after the probe but before the core
	// switched by itself: it stays digital now.
	game_start(0);
	library_probe(-1);
	save_state();
	{
		PadDataS old;
		memcpy(&old, state, sizeof(old));
		assert(old.ds.configModeUsed);
		old.ds.userToggled = 0;
		old.ds.autoAnalogTried = 3;
		memcpy(state, &old, sizeof(old));
	}
	load_state(); ps1_pad_settle();
	expect_ids(0x41, 600, "older state, digital");
	// One saved after the switch keeps it (as it was saved); the shortcut or
	// the Controller row brings it back to digital for good.
	{
		PadDataS old;
		memcpy(&old, state, sizeof(old));
		old.ds.padMode = 1;
		old.ds.autoAnalogTried = 255;
		old.ds.lastUseFrame = frame_counter;   // a real load brings its frame counter too
		memcpy(state, &old, sizeof(old));
	}
	load_state(); ps1_pad_settle();
	expect_ids(0x73, 30, "older state, switched");
	padToggleAnalog(0);
	expect_ids(0x41, 600, "older state, then shortcut");

	puts("ps1-pad: ok");
	return 0;
}
