// Play history and favourites, keyed by save name (playlog.c)
#pragma once
#include <3ds.h>
#include <stdbool.h>

// playlog.c - play history and favourites, keyed by save name
void playlog_init(void);
bool playlog_pending(void);
void playlog_flush(void);                // BreadBox: what a failed write left asked for, asked again                 // at start, before any game (heap layout)
u32 playlog_last(const char *name);      // unix time, 0 = never played
u32 playlog_seconds(const char *name);
u32 playlog_today(u32 now);             // play time today over all games (now: time(NULL)); the room's Quit card
#define PLAYLOG_DAY(t) ((t) / 86400u)    // the day a unix time is in (the console's clock is local time)
bool playlog_favourite(const char *name);
int playlog_favourite_count(void);       // starred games (the bookcase's far view)

bool playlog_toggle_favourite(const char *name);   // returns the new state
void playlog_adopt_key(const char *old, const char *next);
void playlog_adopt_key_later(const char *old, const char *next);
bool playlog_poll_adoptions(void);
void playlog_begin(const char *name);    // a local game starts
void playlog_pause(bool paused);         // pause menu, HOME Menu, sleep
void playlog_end(void);                  // the game ends: time added, file written
void playlog_drop(void);                 // the game never ran (Back at its Continue question): no time, no "last played"
u32 playlog_played(void);                // the running game's total play time so far
