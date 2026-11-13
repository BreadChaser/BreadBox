#ifndef UI_ACTIONS_H
#define UI_ACTIONS_H
/* What a dialog's answer does, as an icon (owner 2026-11-05: no words on buttons, an icon must say what it does).
 * The shared dialog API (ui_confirm_choice, ui_confirm_action, ui_confirm_pick, ui_help_pages) maps each caller's
 * answer ("A  Try again", "B  Other choices", "Next", ...) through this one table; an unknown answer keeps the
 * fallback (check for A, cross for B) and the host test fails, so a new answer must be given its icon here.
 * Needs the UI_ICON_* names (psx3d.h) before it is included. */
#include <string.h>

typedef struct { const char *answer; int icon; } ui_action_icon_t;
static const ui_action_icon_t ui_action_icons[] = {
	{ "Next", UI_ICON_RIGHT },            /* paging: the next page */
	{ "Back", UI_ICON_BACK },             /* paging: the page before; a notice: leave it */
	{ "Done", UI_ICON_CHECK },
	{ "OK", UI_ICON_CHECK },
	{ "Close", UI_ICON_CROSS },
	{ "Cancel", UI_ICON_CROSS },
	{ "Later", UI_ICON_CROSS },           /* not now: like Cancel */
	{ "Quit", UI_ICON_EXIT },
	{ "Try again", UI_ICON_RESCAN },
	{ "Other choices", UI_ICON_TOGGLE },  /* the options icon the shelf uses */
	{ "Play without saving", UI_ICON_PLAY },
	{ "Use an empty card", UI_ICON_PLUS },
	{ "Start empty", UI_ICON_PLUS },      /* a damaged game save: start with an empty one */
	{ "Use it", UI_ICON_CHECK },          /* a save that may be from another emulator: use it */
	{ "Restore backup", UI_ICON_RESET },
	{ "Start over", UI_ICON_START },      /* the Continue question, a damaged game clock: one icon for start over */
	{ "Update", UI_ICON_LOAD },           /* download and install */
	{ "Restore", UI_ICON_RESET },
	{ "Reset", UI_ICON_RESET },           /* also "Reset game", "Reset to defaults", ... (a prefix) */
	{ "Put it back", UI_ICON_BACK },
	{ "Restart", UI_ICON_RESET },
	{ "Continue", UI_ICON_PLAY },
};

/* The answer's words: "A  Try again" -> "Try again" (a key letter and spaces before them are dropped). */
static inline const char *ui_action_words(const char *prompt)
{
	if (!prompt) return "";
	if ((prompt[0] == 'A' || prompt[0] == 'B' || prompt[0] == 'X' || prompt[0] == 'Y') && prompt[1] == ' ') prompt++;
	while (*prompt == ' ') prompt++;
	return prompt;
}

/* The icon for an answer, or fallback when the table has none. Exact words, or "Reset" as a prefix. */
static inline int ui_action_icon(const char *prompt, int fallback)
{
	const char *w = ui_action_words(prompt);
	for (size_t i = 0; i < sizeof(ui_action_icons) / sizeof(ui_action_icons[0]); i++)
		if (!strcmp(w, ui_action_icons[i].answer)) return ui_action_icons[i].icon;
	if (!strncmp(w, "Reset", 5)) return UI_ICON_RESET;
	return fallback;
}
#endif
