// The room's place cards before A (place_cards.c)
#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Place cards summarize their contents before they are opened.
 * What the Collection, Settings and Quit cards show at a glance, as lines on the card's ruled paper:
 * a label, and a value right-aligned like the settings values (or a label alone). Text only, no SDK:
 * home.c fills the shared room card (shelf_card) from these; tests/place_cards checks them.
 * No line restates the card's title, and a card with nothing known gets no lines (never a filler or a 0). */
#define PLACE_LINES 6          // the card's ruled lines under the title (SHELF_CARD_ROWS)
#define PLACE_SYSTEMS_MAX 4    // system lines on the Collection card; the total and favorites take the rest
typedef struct { char label[96], value[24]; } place_line;   // value "" = a label alone
typedef struct { place_line line[PLACE_LINES]; int n; } place_lines;
typedef struct { const char *name, *short_name; int games; } place_system;
/* The card's info row at real metrics: label + gap + value within box, both measured by width (the room's
 * shelf_scene_line_width on the console, the font model in the tests). width NULL: everything fits. A line that
 * does not fit takes its short label (the system's abbreviation, "Today"), never a smaller or cut text. */
typedef struct { float (*width)(const char *s, void *ud); void *ud; float box, gap; } place_fit;

/* Collection: one line per system with games, in the order given (bb_system_display_order), with its count;
 * then the total when more than one system has games (with one it would repeat that line); then the
 * favorites when there are any. known false (the first scan has not finished) with no games found yet:
 * no lines and not empty, since 0 would be wrong. Returns the line count; *empty: the library is known to
 * be empty (the card then says where games go, the one sentence the empty library says everywhere). */
int place_collection(place_lines *out, const place_fit *fit, const place_system *sys, int nsys, int favorites, bool known, bool *empty);
/* Settings: the version, the updater's state in two words (NULL: not known, no line), the room style
 * (NULL: not changeable there, no line). At most 3 lines. */
int place_settings(place_lines *out, const char *version, const char *update, const char *style);
/* Quit: today's play time (0: nothing today, no line; at most a day), and the last game played (NULL/"": no lines),
 * its name on up to two lines (broken at a space). */
int place_quit(place_lines *out, const place_fit *fit, unsigned today_seconds, const char *last_game);
/* A game count as players read it: 1,234 (American grouping). */
void place_count(char *out, size_t cap, long n);
