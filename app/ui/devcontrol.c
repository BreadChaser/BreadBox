#include "app_policy.h"
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "bb_sd_bench.h"
#include "breadbox_import.h"
#include "ht_trace.h"
#include "room.h"
#include "devnet.h"
#include <3ds.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "app_paths.h"
#include "app_config.h"
#include "settings_ui.h"
#include "core_options.h"
#include "platform_video.h"
#include "core_hooks.h"
#include "dev_switches.h"
#include "pmu.h"
#include "ps1_hooks.h"
#include "app_input.h"
#include "presets.h"
#include "library.h"
#include "covers.h"
#include "app_main.h"
#include "ui_font.h"
#include "ui_fixture.h"
#include "ui_trace.h"
#include "bb_registry.h"
void breadbox_return_system(const bb_system_t *system, const char *path);   // breadbox_systems.c: the Library opens on that game
#include "bb_registry.h"
static const char notes_command[] __attribute__((section(".gnu.linkonce.r.bb_notes_command"))) = "whatsnew";
static char system_pending_path[PATH_LEN];
int bb_remote_slot=-1, bb_remote_stereo=-1;
#include "clip.h"
#include "bb_memprobe.h"
#include "../../core/plugins/gpu_pica/gpu_pica_debug.h"
#include "benchstate.h"
#include "widescreen.h"
#include "presetupdate.h"
#include <ctype.h>
#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

bool dev_remote, dev_bench;
char dev_token[96], dev_update_url[256];
static bool exiting, load_pending, change, running, state_failed, command_loading, updated;
/* The game now running was started by a remote load/batch (not from the library). */
static bool remote_game;
static bool batch_active, batch_shot;
static int ui_request;
int dev_ui_slot;
int dev_ui_pending(void) { return ui_request; }
int dev_ui_take(void) { int request = ui_request; ui_request = DEV_UI_NONE; return request; }
bool dev_bench_home;
static char load_line[2048], current_title[PATH_LEN], last_error[160];
static char update_pending[1024];
bool dev_covers_blocked(void) { return update_pending[0] || batch_active || load_pending || running || devnet_import_active(); }
static u64 boot_ms, sample_seq, status_at;
static FILE *tsv, *markdown;
static char (*runs)[2048];
static int run_count, run_index, seconds, warmup;
static u64 run_start, sample_start;
static double sums[DEV_METRICS], weights[DEV_METRICS];
static unsigned samples, black_samples;
static unsigned cmd_id;
static char batch_id[64] = "manual";
static char session_id[32];
static char command_result[128] = "idle";

void dev_authorize(void)
{
	// Only the on-card remote + token pair grants network/install permission.
	if (!dev_token[0]) dev_remote = false;
	if (!dev_remote) dev_update_url[0] = 0;
}
bool dev_enabled(void) { return (dev_remote && dev_token[0]) || dev_bench; }
bool dev_batch_active(void) { return batch_active || clip_active || clip_replay_session; }
static void finish_batch(bool ok);
static void report_run(const char *outcome);
void dev_interrupt(void)
{
	if (!batch_active)
		return;
	// Like log.c, release SD files before APT suspends us. Keeping a file open
	// across HOME previously hung the HOME Menu on real hardware.
	report_run("interrupted");
	finish_batch(false);
	load_pending = false;
	change = true;
}
bool dev_exiting(void) { return exiting; }
bool dev_transition(void) { return change || exiting || load_pending; }
void dev_reply(const char *json)
{
	if (dev_remote)
		devnet_reply(json);
}
static void quote(char *out, size_t size, const char *in);
static void error_reply(const char *message)
{
	if (message != last_error)
		snprintf(last_error, sizeof(last_error), "%s", message);
	// All error messages are internal fixed strings / scrubbed switch words.
	char escaped[1000], json[1100];
	quote(escaped, sizeof(escaped), last_error);
	snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"%s\"}", escaped);
	dev_reply(json);
}
static void quote(char *out, size_t size, const char *in)
{
	size_t i = 0;
	for (; *in && i + 7 < size; in++) {
		unsigned char c = *in;
		if (c == '"' || c == '\\') {
			out[i++] = '\\';
			out[i++] = c;
		} else if (c < 32) {
			snprintf(out + i, size - i, "\\u%04x", c);
			i += 6;
		} else
			out[i++] = c;
	}
	out[i] = 0;
}
static const char *opt(const char *s)
{
	const char *v = options_get(s);
	return v ? v : "";
}
static unsigned heap_free(void)
{
#ifdef __3DS__
	return (unsigned)mallinfo().fordblks;
#else
	return 0;
#endif
}
static unsigned heap_capacity(void)
{
#ifdef __3DS__
 return bb_heap_available();
#else
 return 0;
#endif
}
static unsigned linear_free(void)
{
#ifdef __3DS__
	return linearSpaceFree();
#else
	return 0;
#endif
}
extern int psx3d_idle_skip, psx3d_idle_pointer, psx3d_idle_delay;

/* Recorder commands and status. */
static void clip_status_json(char *out, size_t size)
{
    snprintf(out,size,",\"record\":%s,\"record_seconds\":%d,\"replay\":%d,"
        "\"replay_frame\":%u,\"replay_frames\":%u,\"replay_done\":%s,"
        "\"replay_checks\":%u,\"replay_divergent\":%u,\"replay_checksum_interval\":60",
        clip_enabled?"true":"false",clip_seconds,clip_requested,clip_status.frame,
        clip_status.frames,clip_status.done?"true":"false",clip_status.checks,clip_status.first_divergent);
}
static bool clip_command(const char *command, const char *args, bool playing)
{
    if (strcmp(command,"record")) return false;
    if (batch_active || clip_active) error_reply("clip/batch already active");
    else if (!strcmp(args,"start")) {
        if (playing && emu_record_start()) { ui_request=DEV_UI_RESUME; dev_reply("{\"ok\":true,\"recording\":true}"); }
        else error_reply("record start requires game loaded with record=on");
    } else if (!strcmp(args,"on") || !strcmp(args,"off")) {
        if (playing) error_reply("enable recording in library, then load game");
        else { clip_enabled=!strcmp(args,"on"); dev_reply("{\"ok\":true}"); }
    } else error_reply("record on|off (library) or record start (game)");
    return true;
}
static void publish_status(void)
{
	if (!dev_remote)
		return;
	devnet_busy(running || batch_active || clip_active || command_loading);
	char title[PATH_LEN * 6], err[sizeof(last_error) * 6], json[12288], settings[4096], *p = settings;
	char fs[256], clock[256];
	quote(fs, sizeof(fs), running ? opt("pcsx_rearmed_frameskip_type") : "");
	quote(clock, sizeof(clock), running ? opt("pcsx_rearmed_psxclock") : "");
	quote(title, sizeof(title), current_title);
	quote(err, sizeof(err), last_error);
	p += sprintf(p, "\"frameskip\":\"%s\",\"clock\":\"%s\",\"pmu0\":%d,\"pmu1\":%d,\"overrides\":[", fs,
	             clock, sw_pmu0, sw_pmu1);
	for (int k = 0; k < 8 && sw_opt[k][0]; k++) {
		char escaped[sizeof(sw_opt[0]) * 6];
		quote(escaped, sizeof(escaped), sw_opt[k]);
		p += sprintf(p, "%s\"%s\"", k ? "," : "", escaped);
	}
	sprintf(p, "],\"gldbg\":%d,\"gpu_async\":%s,\"gpu_flush\":%s,\"gpu_thread\":%s,\"nosputhread\":%s",
	        sw_gldbg, sw_gpu_async ? "true" : "false", sw_gpu_flush ? "true" : "false",
	        running && !strcmp(opt("pcsx_rearmed_gpu_thread_rendering"), "enabled") ? "true" : "false",
	        sw_nosputhread ? "true" : "false");
	p += strlen(p);
	p += sprintf(p, ",\"build\":\"%s\",\"rtfs\":false,\"ff\":%s,\"frontend\":true,\"floataudio\":false", BB_BUILD, sw_ff ? "true" : "false");
	p += sprintf(p, ",\"auto_floor\":%d,\"defaults\":%d", game_auto_floor(), defaults_version());
	p += sprintf(p, ",\"fsmart\":\"new\"");
	p += sprintf(p, ",\"remote_game\":%s", remote_game ? "true" : "false");
	p += sprintf(p, ",\"wsbundle\":\"%s\",\"wsscreen\":%d,\"ws_identity\":\"%s\"", sw_wsbundle ? "on" : "off", running ? config_screen_mode() : sw_wsscreen, ws_identity());
	p += sprintf(p, ",\"version\":\"%s\",\"rttcache\":\"%s\"", BB_VERSION, sw_gldbg & PICA_DEBUG_NO_REGION_CACHE ? "old" : "new");
	p += sprintf(p, ",\"preset\":\"%s\",\"preset_version\":%d,\"preset_pack\":%d", game_preset_source(),
	             game_preset_version(), game_presets_pack());
	{
		char pu[128];
		quote(pu, sizeof(pu), presets_update_status());
		p += sprintf(p, ",\"presets_update\":\"%s\"", pu);
	}
	p += sprintf(p, ",\"pica_prims\":%u,\"idle\":%d,\"idleptr\":%d,\"idledelay\":%d", pica_prims, psx3d_idle_skip, psx3d_idle_pointer, psx3d_idle_delay);
	p += sprintf(p, ",\"skipgate\":\"%s\"", psx3d_skipgate ? "new" : "old");
	p += sprintf(p, ",\"texmask\":\"%s\",\"texmemo\":\"%s\"",
	             pica_texmask ? "new" : "old", pica_texmemo ? "new" : "old");
	clip_status_json(p, sizeof(settings)-(size_t)(p-settings)); p+=strlen(p);
	gpustereo_status(p, sizeof(settings)-(size_t)(p-settings)); p+=strlen(p);
    const bb_system_t *active=bb_session_system();
    if(active && active->dev && active->dev->status) {active->dev->status(p,sizeof(settings)-(size_t)(p-settings));p+=strlen(p);}
    p+=sprintf(p,",\"largest_heap\":%u",(unsigned)bb_mem_heap_largest());
	sprintf(p, ",\"rtt\":\"%s\",\"free_heap\":%u,\"free_heap_available\":%u,\"free_linear\":%u",
	        sw_gldbg & PICA_DEBUG_RTT_OLD ? "old" : sw_gldbg & PICA_DEBUG_RTT_NEW ? "new" : "auto", heap_free(), heap_capacity(), linear_free());
	snprintf(json, sizeof(json),
	         "{\"ok\":true,\"session\":\"%s\",\"console\":\"%s\",\"uptime_s\":%.1f,\"mode\":\"%s\","
	         "\"game\":\"%s\",\"state\":%d,\"renderer\":\"%s\",\"settings\":{%s},\"error\":\"%s\","
	         "\"command_id\":%u,\"command_result\":\"%s\",\"bench\":{\"active\":%s,\"id\":\"%s\",\"run\":%d,"
	         "\"runs\":%d,\"samples\":%u}}",
	         session_id, g_new3ds ? "new" : "old", (osGetTime() - boot_ms) / 1000.0,
	         update_pending[0] ? "updating"
	         : running         ? "playing"
	         : load_pending    ? "loading"
	                           : "library",
	         title, sw_state, running ? "gpu" : "", settings, err, cmd_id,
	         command_result, batch_active ? "true" : "false", batch_id, run_index, run_count, samples);
#ifdef BB_UI_FIXTURES
 size_t len=strlen(json);snprintf(json+len-1,sizeof(json)-len+1,",\"ui\":%s}",ui_trace_json());
#endif
	devnet_status(json);
	status_at = osGetTime();
}
static int field_int(const char *line, const char *key, int fallback)
{
	// Keys are only recognized at token boundaries.
	size_t n = strlen(key);
	for (const char *p = line; (p = strstr(p, key)); p++)
		if ((p == line || isspace((unsigned char)p[-1])) && p[n] == '=') {
			char *end;
			long value = strtol(p + n + 1, &end, 10);
			if (end == p + n + 1 || (*end && !isspace((unsigned char)*end)) || value < 0 || value > 3600)
				return -1;
			return value;
		}
	return fallback;
}
static void finish_batch(bool ok)
{
	batch_active = false;
	if (markdown) {
		fprintf(markdown, "\n%s. Black counts are near-black **one-second probes**, not all video frames.\n",
		        ok ? "Completed" : "Stopped on first failure");
		fclose(markdown);
		markdown = NULL;
	}
	if (tsv) {
		fclose(tsv);
		tsv = NULL;
	}
	free(runs);
	runs = NULL;
	if (dev_bench_home && ok)
		exiting = true;
	publish_status();
}
static void report_run(const char *outcome)
{
	if (!tsv || !markdown)
		return;
	// TSV stores the exact input in the final column for reproducibility.
	fprintf(tsv, "%s\t%d\t%s\t%u\t%u", batch_id, run_index + 1, outcome, samples, black_samples);
	for (int i = 0; i < DEV_METRICS; i++) {
		if (weights[i])
			fprintf(tsv, "\t%.6f", sums[i] / weights[i]);
		else
			fputs("\tNA", tsv);
	}
	fprintf(tsv, "\t%u\t%u\t%u\t%s\n", clip_status.replay?clip_status.frame:0,
	        clip_status.replay?clip_status.checks:0,clip_status.replay?clip_status.first_divergent:0,runs[run_index]);
	fflush(tsv);
	fprintf(markdown, "| %d | %s | %.2f | %.1f | %.2f | %.3f | %u |\n", run_index + 1, outcome,
	        weights[0] ? sums[0] / weights[0] : 0, weights[1] ? sums[1] / weights[1] : 0,
	        weights[2] ? sums[2] / weights[2] : 0, weights[6] ? sums[6] / weights[6] : 0, black_samples);
	fflush(markdown);
}
static bool begin_batch(const char *text)
{
	if (batch_active)
		return false;
	runs = calloc(32, sizeof(*runs));
	if (!runs)
		return false;
	run_count = run_index = 0;
	char *copy = strdup(text ? text : "");
	if (!copy) {
		free(runs);
		runs = NULL;
		return false;
	}
	char *ctx, *line = strtok_r(copy, ";\n", &ctx);
	for (; line; line = strtok_r(NULL, ";\n", &ctx)) {
		while (isspace((unsigned char)*line))
			line++;
		if (!*line || *line == '#')
			continue;
		for (char *p = line; *p; p++)
			if (*p == '\t' || *p == '\r')
				*p = ' ';
		if (run_count == 32 || strlen(line) >= sizeof(*runs)) {
			run_count = 0;
			break;
		}
		strcpy(runs[run_count++], line);
	}
	free(copy);
	if (!run_count) {
		free(runs);
		runs = NULL;
		return false;
	}
	mkdir(PSX3D_DIR "/bench", 0777);
	tsv = fopen(PSX3D_DIR "/bench/results.tsv", "w");
	markdown = fopen(PSX3D_DIR "/bench/results.md", "w");
	if (!tsv || !markdown) {
		finish_batch(false);
		return false;
	}
	fputs("batch\trun\tstatus\tsamples\tblack_samples", tsv);
	for (int i = 0; i < DEV_METRICS; i++)
		fprintf(tsv, "\t%s", dev_columns[i]);
	fputs("\treplay_frames\treplay_checks\treplay_divergent\trequest\n", tsv);
	fflush(tsv);
	fprintf(markdown,
	        "# " APP_NAME " batch %s (%s 3DS)\n\n| Run | Status | FPS | Speed %% | Emu ms | M cycles/frame | Black "
	        "probes |\n|---|---|---:|---:|---:|---:|---:|\n",
	        batch_id, g_new3ds ? "New" : "Old");
    system_pending_path[0]=0;bb_remote_slot=bb_remote_stereo=-1;
	batch_active = true;
	strcpy(load_line, runs[0]);
	load_pending = change = true;
	return true;
}
static void bench_file(void)
{
	if (!dev_bench) {
		snprintf(last_error, sizeof(last_error), "bench.txt requires bench in switches.txt");
		return;
	}
	FILE *f = fopen(PSX3D_DIR "/bench.txt", "r");
	if (!f) {
		snprintf(last_error, sizeof(last_error), "bench.txt missing");
		return;
	}
	char *s = calloc(1, DEV_COMMAND_MAX);
	if (s) {
		size_t n = fread(s, 1, DEV_COMMAND_MAX - 1, f);
		if (n && feof(f))
			begin_batch(s);
		else
			snprintf(last_error, sizeof(last_error), "bench.txt empty or too large");
		free(s);
	}
	fclose(f);
}

// Convert load's title/path prefix (spaces accepted) into the bench game= token.
// The first key=value starts settings; underscores are the wire escape for spaces.
static bool load_request(const char *args)
{
	if (strlen(args) >= sizeof(load_line) / 3 - 6)
		return false;
	if (!strncmp(args, "game=", 5))
		strcpy(load_line, args);
	else {
		bool path = !strncmp(args, "sdmc:/", 6);
		strcpy(load_line, "game=");
		char *out = load_line + 5;
		for (const char *p = args; *p; p++) {
			if (*p == ' ') {
				const char *next = p + 1;
				size_t n = strcspn(next, " ");
				if (memchr(next, '=', n)) {
					strcpy(out, p);
					break;
				}
				if (path) {
					memcpy(out, "%20", 3);
					out += 3;
				} else
					*out++ = '_';
			} else
				*out++ = *p;
			*out = 0;
		}
	}
	load_pending = change = command_loading = true;
	return true;
}
static bool runtime_set(const char *word)
{
	// dev_tick runs after pica_thread_sync. These arithmetic choices change
	// no queued GPU state or texture lifetimes, so they can change live.
	if (!strncmp(word, "pica_prims_", 11))
		return switches_set(word);
	// Conservative allowlist: renderer/thread/CD/SPU and arbitrary core option
	// changes require a clean load. No pretending unsafe settings changed live.
	if (!strncmp(word, "frameskip=", 10) || !strncmp(word, "clock=", 6) ||
	    !strncmp(word, "o:frameskip_", 12) || !strncmp(word, "slider=", 7) || !strncmp(word, "stereo=", 7) || !strncmp(word, "gldbg=", 6)) {
		bool ok = switches_set(word);
		if (ok && !strncmp(word, "gldbg=", 6)) {
			extern int pica_debug;
			pica_debug = sw_gldbg;
		}
		return ok;
	}
	return false;
}
// Screens that cannot be left mid-way (the resume prompt) only take keys and shots.
static bool keys_only;
void dev_tick_keys(void)
{
	keys_only = true;
	dev_tick(false);
	keys_only = false;
}
bool dev_tick(bool playing)
{
	if (!dev_enabled())
		return false;
	char command[DEV_COMMAND_MAX];
	if (devnet_take(command)) {
		cmd_id++;
		last_error[0] = 0;
		snprintf(command_result, sizeof(command_result), "ok");
		char *args = strchr(command, ' ');
		if (args) {
			*args++ = 0;
			while (*args == ' ')
				args++;
		} else
			args = command + strlen(command);
        const bb_system_t *requested_system=NULL;
        for(unsigned i=0;i<bb_system_count;i++) {
            const bb_system_t *s=bb_systems[i];char alias[24],field[24];
            snprintf(alias,sizeof(alias),"%s-load",s->id);
            snprintf(field,sizeof(field),"system=%s",s->id);
            const char *match=strstr(args,field);
            if(!strcmp(command,alias) || (!strcmp(command,"load") && match &&
                (match==args || match[-1]==' ') && (!match[strlen(field)] || match[strlen(field)]==' ')))requested_system=s;
        }
		if (keys_only && strcmp(command, "ui-key") && strcmp(command, "shot")) {
			error_reply("only ui-key and shot on this screen");
		} else if (clip_active && strcmp(command,"quit") && strcmp(command,"exit") && strcmp(command,"shot")) {
			error_reply("clip active; quit to cancel before changing settings/UI");
		} else if (ui_fixture_command(command,args)) { } else if (!strcmp(command, "ui-key")) {
			if (batch_active || !(
                (bb_session_system() && bb_session_system()->dev && bb_session_system()->dev->key && dev_ui_pending()==DEV_UI_NONE && bb_session_system()->dev->key(args,1)) ||
                settings_ui_debug_key(args) || input_remote_key(args)))
				error_reply("ui-key needs an open menu; a b x y up down left right l r select");
			else dev_reply("{\"ok\":true}");
		} else if (!strcmp(command, "ui")) {
			char *option = strchr(args, ' ');
			if (option) *option++ = 0;
			int screen = !strcmp(args, "library") ? DEV_UI_LIBRARY :
			             !strcmp(args, "pause") ? DEV_UI_PAUSE :
			             !strcmp(args, "settings") ? DEV_UI_SETTINGS :
			             !strcmp(args, "states") ? DEV_UI_STATES :
			             !strcmp(args, "resume") ? DEV_UI_RESUME :
			             !strcmp(args, "continue") ? DEV_UI_CONTINUE :
			             !strcmp(args, "welcome") ? DEV_UI_WELCOME : !strcmp(args, notes_command) ? DEV_UI_WHATSNEW : DEV_UI_NONE;
			int slot = option && !strncmp(option, "slot=", 5) ? field_int(option, "slot", 0) : 0;
			bool valid = screen && slot >= 0 && slot < SLOT_COUNT;
			const game_t *selection = NULL;
			if (option && *option) {
				if (screen == DEV_UI_LIBRARY && !strncmp(option, "select=", 7)) {
					char *title = option + 7;
					for (char *c = title; *c; c++) if (*c == '_') *c = ' ';
					const game_t *picked = NULL;
					for (int i = 0; i < library_count(); i++)
						if (!strcasecmp(library_get(i)->title, title)) picked = library_get(i);
					if (picked) selection = picked; else valid = false;
				} else if (screen != DEV_UI_STATES || strncmp(option, "slot=", 5)) valid = false;
			}
			if (!valid || batch_active || (!playing && screen != DEV_UI_LIBRARY && screen != DEV_UI_SETTINGS && screen != DEV_UI_WELCOME && screen != DEV_UI_WHATSNEW) ||
			    (playing && (screen == DEV_UI_WELCOME || screen == DEV_UI_WHATSNEW)))
				error_reply("ui library [select=title] | pause | settings | welcome | states [slot=" SLOT_RANGE "] | resume | continue; idle batch required");
			else {
				char at[PATH_LEN];   // the Library (the room's bookcase) opens on that game, by the path it starts from
				if (selection && library_launch_path(selection, at, sizeof(at))) {
					const bb_system_t *s = bb_system_for_path(at);
					if (s) breadbox_return_system(s, at);
				}
				ui_request = screen;
				dev_ui_slot = slot;
				if (screen == DEV_UI_LIBRARY) {
					change = playing;
				}
				dev_reply("{\"ok\":true}");
			}
		} else if (clip_command(command,args,playing)) {
		} else if (!strcmp(command, "covers")) {
			if (!*args) {
				char info[2048];
				covers_debug(info, sizeof(info));
				dev_reply(info);
			} else if (!strcmp(args, "stats-reset")) {
				covers_stats_reset();
				dev_reply("{\"ok\":true}");
			} else if (!strncmp(args, "sdms=", 5)) {
				covers_sd_ms = strtoul(args + 5, NULL, 10);   // emulator: model the SD card
				dev_reply("{\"ok\":true}");
			} else if (!strcmp(args, "fast=off") || !strcmp(args, "fast=on")) {
				covers_slow = args[6] == 'f';   // A/B: small covers, scroll order, PNG listing
				dev_reply("{\"ok\":true}");
			} else {
				int game = -1;
				if (!playing && !batch_active && !strncmp(args, "refresh=", 8)) {
					char *name = args + 8, *end;
					long index = strtol(name, &end, 10);
					if (*name && !*end && index >= 0 && index < library_count()) game = index;
					else {
						for (char *p = name; *p; p++) if (*p == '_') *p = ' ';
						for (int i = 0; i < library_count(); i++)
							if (!strcasecmp(library_get(i)->title, name)) game = i;
					}
				}
				if (game >= 0 && covers_refresh(game)) dev_reply("{\"ok\":true}");
				else error_reply("covers [refresh=title_or_index] requires idle library");
			}
		} else if (!strcmp(command, "presets-update")) {
			// Test the daily preset check against a LAN server: "now [url=http://...]";
			// "reset" deletes downloaded packs and the stamp (library only).
			char *url = strstr(args, "url=");
			bool reset = !strcmp(args, "reset");
			bool now = !strncmp(args, "now", 3);
			if (url) url += 4;
			if (url && (strncmp(url, "http", 4) || strlen(url) >= 96 || strchr(url, '"')))
				error_reply("presets-update [now] [url=http://numeric-ip:port/path/] | reset");
			else if (reset && (playing || batch_active || load_pending))
				error_reply("presets-update reset requires the idle library");
			else {
				if (reset) {
					if(!covers_stop_wait()){load_pending=batch_active=change=command_loading=false;covers_resume();error_reply("Background work still active");return dev_transition();}
					bool done = presets_update_dev_reset();
					covers_resume();
					if (!done) { error_reply("presets-update reset: could not remove a folder"); goto presets_done; }
				}
				if (url || now) presets_update_dev(url, now);
				char info[192], st[128];
				quote(st, sizeof(st), presets_update_status());
				snprintf(info, sizeof(info), "{\"ok\":true,\"status\":\"%s\",\"due\":%s,\"pack\":%d}", st,
				         presets_update_due() ? "true" : "false", game_presets_pack());
				dev_reply(info);
			}
			presets_done:;
        } else if (requested_system || (!strcmp(command,"load") && strstr(args,"system="))) {
            char title[256]="",path[PATH_LEN];int used=0,slot=-1,stereo=-1;
            bool valid=requested_system && sscanf(args,"%255s%n",title,&used)==1;
            if(requested_system && requested_system->dev && requested_system->dev->option)requested_system->dev->option(NULL);
            for(char *p=args+used;valid && *p;) {
                char word[64];int n=0,v;char tail;
                if(sscanf(p," %63s%n",word,&n)!=1)break;
                if(sscanf(word,"state=%d%c",&v,&tail)==1 && v>=0 && v<SLOT_COUNT)slot=v;   /* the card index: shown number - 1 */
                else if((sscanf(word,"stereo=%d%c",&v,&tail)==1 || sscanf(word,"layer3d=%d%c",&v,&tail)==1) && v>=0 && v<=2)stereo=v;
                else if(!strncmp(word,"system=",7) && !strcmp(word+7,requested_system->id)) {}
                else if(!requested_system->dev || !requested_system->dev->option || !requested_system->dev->option(word))valid=false;
                p+=n;
            }
            if(strncmp(title,"sdmc:/",6))for(char *p=title;*p;p++)if(*p=='_')*p=' ';
            bool found=false;
            if(valid) {
                if(!strncmp(title,"sdmc:/",6) && bb_system_for_path(title)==requested_system) {
                    snprintf(path,sizeof(path),"%s",title);found=true;
                } else if(requested_system->shell && requested_system->shell->resolve)
                    found=requested_system->shell->resolve(title,path,sizeof(path));
            }
            if(!valid || batch_active || !found)error_reply("load TITLE system=ID [state=0..9] [stereo=0|1|2]; missing/ambiguous title");
            else {
                snprintf(system_pending_path,sizeof(system_pending_path),"%s",path);
                bb_remote_slot=slot;bb_remote_stereo=stereo;
                ui_request=DEV_UI_NONE;load_pending=change=command_loading=true;
                if(!covers_stop_wait()){load_pending=batch_active=change=command_loading=false;covers_resume();error_reply("Background work still active");return dev_transition();}
            }
		} else if (!strcmp(command, "load")) {
            system_pending_path[0]=0;bb_remote_slot=bb_remote_stereo=-1;
			ui_request = DEV_UI_NONE;
			if (batch_active)
				error_reply("batch active; quit to cancel first");
			else if (!*args || !load_request(args))
				error_reply("load requires a game or path (max 675 bytes)");
			else if(!covers_stop_wait()){load_pending=batch_active=change=command_loading=false;covers_resume();error_reply("Background work still active");return dev_transition();}
			// load reply is sent only once loading + initial state have completed.
		} else if (!strcmp(command, "ff")) {
			if (!playing || batch_active || (strcmp(args, "on") && strcmp(args, "off"))) error_reply("ff on|off requires game and idle batch");
			else { sw_ff = !strcmp(args, "on"); dev_reply("{\"ok\":true}"); }
		} else if (!strcmp(command, "set") && (!strcmp(args, "rtt=old") || !strcmp(args, "rtt=new") || !strcmp(args, "rtt=auto"))) {
			// RTT maps/cache generations cannot change in a live renderer. Repeat
			// the last explicit load, retaining its state/settings, at a clean boundary.
			if (batch_active || !playing)
				error_reply("RTT setting requires a game and idle batch");
			else {
				char copy[sizeof(load_line)], next[sizeof(load_line)] = "", *ctx;
				strcpy(copy, load_line);
				for (char *w = strtok_r(copy, " ", &ctx); w; w = strtok_r(NULL, " ", &ctx)) {
					if (!strncmp(w, "rtt=", 4)) continue;
					if (next[0]) strcat(next, " ");
					strcat(next, w);
				}
				if (strlen(next) + strlen(args) + 2 > sizeof(load_line))
					error_reply("load settings too long");
				else {
					strcat(next, " "); strcat(next, args);
					strcpy(load_line, next);
					load_pending = change = command_loading = true;
				}
			}
		} else if (!strcmp(command, "set")) {
			if (clip_active || batch_active || !playing || !runtime_set(args))
				error_reply("setting requires load, or batch active/no game");
			else
				dev_reply("{\"ok\":true}");
		} else if (!strcmp(command, "state-import")) {
            char title[256], hash[65], extra;
            unsigned size, slot;
            const game_t *picked = NULL;
            if (!dev_remote || playing || load_pending || batch_active || clip_replay_session ||
                sscanf(args, "%255s slot=%u size=%u sha256=%64s %c", title, &slot, &size, hash, &extra) != 4 ||
                slot < 5 || slot > 7 || size != BENCH_STATE_BYTES || strlen(hash) != 64) {
                error_reply("state-import TITLE slot=5..7 size=4456448 sha256=HASH requires idle library");
            } else {
                bool valid = true;
                for (unsigned i = 0; i < 64; i++) if (!isxdigit((unsigned char)hash[i])) valid = false;
                for (char *p = title; *p; p++) if (*p == '_') *p = ' ';
                unsigned matches = 0;
                for (int i = 0; i < library_count(); i++)
                    if (!strcasecmp(library_get(i)->title,title)) { picked = library_get(i); matches++; }
                char path[PATH_LEN];
                struct stat st;
                if (!valid || matches != 1 || !bench_state_name(picked->cover_key))
                    error_reply("invalid/ambiguous game or hash");
                else {
                    snprintf(path,sizeof(path),PSX3D_STATE_DIR "/%s.st%u",picked->cover_key,slot);
                    if (!stat(path,&st) || errno != ENOENT) error_reply("benchmark slot already exists or cannot be checked");
                    else {
                        if(!covers_stop_wait()){load_pending=batch_active=change=command_loading=false;covers_resume();error_reply("Background work still active");return dev_transition();}
                        devnet_import_reply(path,hash);
                    }
                }
            }
		} else if (!strcmp(command, "savestate") || !strcmp(command, "loadstate")) {
			char *end;
			long slot = strtol(args, &end, 10);
			if (!playing || clip_active || batch_active || !*args || *end || slot < 0 || slot >= SLOT_COUNT)
				error_reply("state requires game, idle batch, slot " SLOT_RANGE);
            else if (!bb_session_system() || !bb_session_system()->dev || !bb_session_system()->dev->state ||
                     !bb_session_system()->dev->state(bb_session_system()->save ? bb_slot_file((int)slot) : (int)slot,!strcmp(command,"savestate")))   /* the shared save service has its own file numbers (bb_slots.h) */
				error_reply("state operation failed");
			else
				dev_reply("{\"ok\":true}");
		}
#ifdef BB_IMPORT_RECOVERY_TEST
        else if (!strcmp(command,"recover-probe")) {
            extern bool safe_recovery_probe(void);
            if(playing || batch_active || breadbox_import_busy())error_reply("recovery probe requires idle library/import");
            else {if(!covers_stop_wait()){load_pending=batch_active=change=command_loading=false;covers_resume();error_reply("Background work still active");return dev_transition();}bool ok=safe_recovery_probe();covers_resume();if(ok)dev_reply("{\"ok\":true,\"missing_errno\":2,\"restored\":true,\"removed\":true}");else error_reply("recovery probe failed; stop and inspect test files");}
        }
#endif
        else if (!strcmp(command, "sd-bench")) {
            if(playing || breadbox_import_busy() || *args || !bb_sd_bench_start())error_reply("SD benchmark requires idle Library and finished import");
            else dev_reply("{\"ok\":true}");
        } else if (!strcmp(command, "ht")) {
            room_pace_diagnostic();
#ifdef HT_TRACE
            ht_dump();dev_reply("{\"ok\":true}");
#else
            error_reply("HT trace not compiled");
#endif
        } else if (!strcmp(command, "shot")) {
            room_pace_diagnostic();
			if (*args && strcmp(args, "both") && strcmp(args, "top") && strcmp(args, "eyes"))
				error_reply("shot [top|both|eyes]");
			else if (!(!strcmp(args, "eyes") ? devnet_shot_eyes(NULL) : devnet_shot(NULL, (!*args || !strcmp(args, "both")))))
				error_reply("screenshot busy or allocation failed");
			else
				dev_reply("{\"ok\":true}"); // worker sends PNG instead of JSON
		} else if (!strcmp(command, "bench")) {
			ui_request = DEV_UI_NONE;
			if (batch_active) {
				error_reply("batch already active");
				publish_status();
				return dev_transition();
			}
			if (!*args || !strcmp(args, "file"))
				bench_file();
			else if (!begin_batch(args))
				error_reply("invalid batch; max 32 runs/2047 bytes each");
			if (batch_active) {
				if(!covers_stop_wait()){load_pending=batch_active=change=command_loading=false;covers_resume();error_reply("Background work still active");return dev_transition();}
				dev_reply("{\"ok\":true,\"bench\":\"started\"}");
			}
			else
				error_reply("batch did not start");
		} else if (!strcmp(command, "batch-id")) {
			bool ok = *args && strlen(args) < sizeof(batch_id);
			for (char *p = args; *p; p++)
				if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_')
					ok = false;
			if (!ok || batch_active)
				error_reply("invalid batch id or active batch");
			else {
				strcpy(batch_id, args);
				dev_reply("{\"ok\":true}");
			}
		} else if (!strcmp(command, "quit") || !strcmp(command, "exit")) {
			ui_request = DEV_UI_NONE;
			if (batch_active) {
				report_run("cancelled");
				finish_batch(false);
			}
			change = true;
			exiting = !strcmp(command, "exit");
			load_pending = false;
            system_pending_path[0]=0;bb_remote_slot=bb_remote_stereo=-1;
			dev_reply("{\"ok\":true}");
		} else if (!strcmp(command, "update")) {
			ui_request = DEV_UI_NONE;
			if (batch_active || !*args || strlen(args) >= sizeof(update_pending))
				error_reply("update requires URL and no active batch");
			else {
				if(!covers_stop_wait()){load_pending=batch_active=change=command_loading=false;covers_resume();error_reply("Background work still active");return dev_transition();}
				strcpy(update_pending, args);
				change = true;
				if (!playing)
					devnet_update_reply(args);
			}
		} else if (!strcmp(command, "_update_failed")) {
			update_pending[0] = 0;
			covers_resume();
			snprintf(last_error, sizeof(last_error), "update failed; see host response or log");
			devnet_notification_done();
		} else if (!strcmp(command, "_updated")) {
			updated = true;
			exiting = true;
			devnet_notification_done();
		} else
			error_reply("unknown command");
		publish_status();
	}
	if (osGetTime() - status_at >= 1000)
		publish_status();
	return dev_transition();
}

bool dev_remote_game(void) { return remote_game; }
void dev_local_game(void)
{
	remote_game = false;
    if(!load_pending)bb_remote_slot=bb_remote_stereo=-1;
	switches_local();
	publish_status();
}
bool dev_next_game(char *path, char *save, char *title)
{
	if (!load_pending) {
		change = false;
		return false;
	}
	load_pending = change = false;
    if(system_pending_path[0]) {
        snprintf(path,PATH_LEN,"%s",system_pending_path);system_pending_path[0]=0;
        save[0]=0;snprintf(title,PATH_LEN,"%s",strrchr(path,'/')+1);
        remote_game=true;snprintf(current_title,sizeof(current_title),"%s",title);
        publish_status();return true;
    }
	char copy[sizeof(load_line)], game[PATH_LEN], error[160] = "invalid run", want[PATH_LEN];
	strcpy(copy, load_line);
	warmup = field_int(copy, "warmup", 5);
	seconds = field_int(copy, "seconds", 20);
	int shot_value = field_int(copy, "shot", 1);
	batch_shot = shot_value != 0;
	memset(sums, 0, sizeof(sums));
	memset(weights, 0, sizeof(weights));
	samples = black_samples = 0;
	state_failed = false;
	if (shot_value < 0 || shot_value > 1 || warmup < 0 || warmup > 3600 || seconds < 1 || seconds > 3600 ||
	    !switches_run(copy, game, sizeof(game), error, sizeof(error))) {
		snprintf(last_error, sizeof(last_error), "%.150s", error);
		dev_game_ended(false);
		return false;
	}
	snprintf(want, sizeof(want), "%s", game);
	for (char *p = want; *p; p++)
		if (*p == '_')
			*p = ' ';
	const game_t *picked = NULL;
	int matches = 0;
	for (int i = 0; i < library_count(); i++) {
		const game_t *g = library_get(i);
		if (!strcasecmp(g->title, want)) {
			picked = g;
			matches = 1;
			break;
		}
		if (strcasestr(g->title, want)) {
			picked = g;
			matches++;
		}
	}
	if (picked && matches == 1 && !library_launch_path(picked, path, PATH_LEN)) {
		snprintf(last_error, sizeof(last_error), "could not write the disc list for that game");
		dev_game_ended(false);
		return false;
	}
	if (picked && matches == 1) {
		snprintf(save, PATH_LEN, "%s", picked->cover_key);
		snprintf(title, PATH_LEN, "%s", picked->title);
	} else if (!strncmp(game, "sdmc:/", 6) && strlen(game) < PATH_LEN) {
		// Literal paths keep underscores. Titles use underscore-for-space matching.
		char *dst = path;
		for (char *src = game; *src; src++) {
			if (!strncmp(src, "%20", 3)) {
				*dst++ = ' ';
				src += 2;
			} else
				*dst++ = *src;
		}
		*dst = 0;
		save[0] = 0;
		snprintf(title, PATH_LEN, "%s", game + 6);
	} else {
		snprintf(last_error, sizeof(last_error), "game missing/ambiguous; use exact title or sdmc:/ path");
		dev_game_ended(false);
		return false;
	}
	remote_game = true;
	snprintf(current_title, sizeof(current_title), "%s", title);
	strcpy(command_result, "loading");
	publish_status();
	return true;
}
void dev_game_started(const char *title, bool state_ok)
{
	if (!dev_enabled())
		return;
	running = true;
	state_failed = !state_ok;
	snprintf(current_title, sizeof(current_title), "%s", title);
	devnet_sample(NULL, true);
	run_start = osGetTime();
	sample_start = clip_status.replay ? run_start : 0;
	if (!state_ok) {
		snprintf(last_error, sizeof(last_error), "requested state failed to load");
		/* A player's refused Continue keeps the battery boot; only remote loads end. */
		change = remote_game;
	} else
		last_error[0] = 0;
	snprintf(command_result, sizeof(command_result), "%s", state_ok ? "loaded" : "state failed");
	if (command_loading) {
		dev_reply(state_ok ? "{\"ok\":true,\"loaded\":true}"
		                   : "{\"ok\":false,\"error\":\"state load failed\"}");
		command_loading = false;
	}
	publish_status();
}
void dev_game_ended(bool ok)
{
	if (!dev_enabled())
		return;
	running = false;
	current_title[0] = 0;
	// A direct load/set first tears down the previous game. Its completion
	// reply belongs to the next game, whose request is still pending here.
	if (command_loading && !load_pending) {
		error_reply(last_error[0] ? last_error : "game failed to load");
		command_loading = false;
	}
	if (update_pending[0])
		devnet_update_reply(update_pending);
	if (batch_active) {
		if (!ok || state_failed || !samples) {
			load_pending = false;
			report_run(state_failed ? "state_failed" : "load_failed");
			finish_batch(false);
		} else if (!change) {
			report_run("interrupted");
			finish_batch(false);
		}
	}
	publish_status();
}
void dev_sample(const statsview_sample *s, const char *probe, int top_lum, double cycles, double ev0,
                double ev1)
{
	if (!dev_enabled())
		return;
	const emu_frontend_sample *front = emu_frontend_stats();
	dev_measurement m = {.seq = ++sample_seq, .ms = osGetTime()};
	double values[DEV_METRICS] = {
	    s->fps,  s->target && s->elapsed ? statsview_emulated() * 100000.0 / s->target / s->elapsed : 0,
	    s->emu,  s->present,
	    s->wait, s->gpu,
	    cycles,  ev0,
	    ev1,     s->skipped,
	    s->late, -1,
	    -1,      -1,
	    -1,      -1,
	    -1,      -1,
	    -1,      -1,
	    -1,      -1,
	    top_lum, s->xfer,
	    s->heap, s->queue,
	    s->gap, front->run_ms, front->present_ms, front->audio_ms, front->misc_ms, front->pace_ms,
	    front->top, front->bottom, front->held, sw_ff, front->spu_ms, front->spu_wait_ms,
	    front->skip_level, front->drawn, front->skipped, -1, -1, -1, front->pica_ms};
	memcpy(m.v, values, sizeof(values));
 for (int k=0; k<8; k++) m.v[45+k] = (sw_gldbg & PICA_DEBUG_PHASE_TIMING) ? front->phase_ms[k] : -1;
 double category_cycles[3]; unsigned category_count[3];
 pmu_categories(category_cycles, category_count);
 pmu_renderer(&m.v[59]);
 pmu_category_detail(&m.v[63]);
 m.v[84] = front->draw_fraction;
 m.v[85] = front->picture_mean; m.v[86] = front->picture_square;
 m.v[87] = front->picture_intervals;
 for (int k=0; k<3; k++) { m.v[53+k] = category_cycles[k]; m.v[56+k] = category_count[k]; }
	const char *ap = s->audio ? strstr(s->audio, "underruns=") : NULL;
	unsigned au, ad;
	if (ap && sscanf(ap, "underruns=%u drop=%u", &au, &ad) == 2) { m.v[41] = au; m.v[42] = ad; }
	ap = s->audio ? strstr(s->audio, "maxgap=") : NULL;
	if (ap) m.v[43] = atof(ap + 7);
	snprintf(m.audio, sizeof(m.audio), "%s", s->audio ? s->audio : "");
	snprintf(m.pica, sizeof(m.pica), "%s", s->renderer ? s->renderer : "");
	snprintf(m.probe, sizeof(m.probe), "%s", probe);
	if (renderer_is_gpu()) {
		unsigned v[8] = {0};
		// Stats may insert diagnostic sections before the stable counter labels.
		const char *p = strstr(m.pica, " batches ");
		if (p)
			while (p > m.pica && isdigit((unsigned char)p[-1])) p--;
		if (p && sscanf(p,
		                "%u batches %u prims %u tile decodes (%u evictions %u invalidations) %u uploads %u "
		                "syncs %u readbacks",
		                &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) == 8)
			for (int i = 0; i < 8; i++)
				m.v[11 + i] = v[i] * 1000.0 / s->elapsed;
		unsigned gpu = 0, none = 0, cpu = 0;
		if (sscanf(probe, "pic gpu %u none %u cpu %u", &gpu, &none, &cpu) == 3) {
			m.v[19] = gpu;
			m.v[20] = none;
		}
		p = strstr(probe, " lum ");
		if (p)
			m.v[21] = atoi(p + 5);
	}
	devnet_sample(&m, false);
	if (!batch_active || state_failed)
		return;
	// Discard warm-up *and* its last partial interval. Begin sampling on the
	// next complete per-second interval, and never include unload/PNG SD writes.
	if (!sample_start) {
		if (m.ms - run_start >= (u64)warmup * 1000)
			sample_start = m.ms;
		return;
	}
	for (int i = 0; i < DEV_METRICS; i++)
		if (m.v[i] >= 0) {
			// Presentation moments are weighted by intervals, so variance is
			// exact across a batch even when cadence changes between samples.
			double weight = (i == 85 || i == 86) ? front->picture_intervals : s->elapsed;
			sums[i] += m.v[i] * weight;
			weights[i] += weight;
		}
	samples++;
	if ((m.v[21] >= 0 && m.v[21] <= 1) || (top_lum >= 0 && top_lum <= 2))
		black_samples++;
	if (clip_status.replay ? clip_status.done : m.ms - sample_start >= (u64)seconds * 1000) {
		if (batch_shot) {
			char path[PATH_LEN];
			snprintf(path, sizeof(path), PSX3D_DIR "/bench/%s-%02d.png", batch_id, run_index + 1);
			if (!devnet_shot(path, false))
				snprintf(last_error, sizeof(last_error), "screenshot allocation failed");
		}
		bool replay_ok=!clip_status.replay || (!clip_status.failed && clip_status.done);
		report_run(replay_ok?"ok":"replay_diverged");
		if (!replay_ok) { finish_batch(false); change=true; publish_status(); return; }
		run_index++;
		if (run_index < run_count) {
			strcpy(load_line, runs[run_index]);
			load_pending = true;
		} else
			finish_batch(true);
		change = true;
	}
	publish_status();
}
void dev_init(void)
{
	dev_authorize();
	if (!dev_enabled())
		return;
	boot_ms = osGetTime();
	snprintf(session_id, sizeof(session_id), "%llx", svcGetSystemTick());
	ui_font_log();   // the fonts' own metrics (the cards' text stands on the loaded font's baseline)
	devnet_init();
	if (dev_bench && !dev_update_url[0])
		bench_file();
	if (dev_update_url[0]) {
		strcpy(update_pending, dev_update_url);
		devnet_start_update(dev_update_url);
	}
	publish_status();
}
/* False if the network worker did not stop in time: its socket and buffers stay. */
bool dev_shutdown_timeout(uint64_t timeout)
{
	if (!dev_enabled())
		return true;
	if (batch_active) {
		report_run("interrupted");
		finish_batch(false);
	}
	if (!devnet_exit_timeout(timeout))
		return false;
	if (updated && !envIsHomebrew())
		aptSetChainloaderToSelf();
	return true;
}
void dev_shutdown(void)
{
	(void)dev_shutdown_timeout(UINT64_MAX);
}

void dev_system_sample(const char *system, double elapsed, unsigned emulated, unsigned drawn, unsigned shown, double emu_ms,
                    double present_ms, const char *audio, double guest_fps, const char *fields)
{
 if(!dev_remote)return;
 dev_measurement m={0};for(int i=0;i<DEV_METRICS;i++)m.v[i]=-1;
 m.seq=++sample_seq;m.ms=osGetTime();
 unsigned pictures=drawn<shown?drawn:shown;
 m.v[0]=pictures/elapsed;m.v[1]=emulated*100.0/(guest_fps*elapsed);
 m.v[2]=emu_ms;m.v[3]=present_ms;m.v[32]=shown;m.v[39]=drawn;m.v[40]=emulated>pictures?emulated-pictures:0;
 const char *u=strstr(audio,"underruns=");if(u)m.v[41]=strtoul(u+10,NULL,10);
 snprintf(m.audio,sizeof(m.audio),"%s",audio);
 snprintf(m.probe,sizeof(m.probe),"system=%s emulated=%u drawn=%u shown=%u elapsed=%.6f %s",system,emulated,drawn,shown,elapsed,fields?fields:"");
 devnet_sample(&m,false);
}
