// The memory card question before a PS1 game runs (moved out of emu.c): a card that could not
// be read is asked about before the game plays on an empty stand-in. Only the shared dialog
// (ui_confirm_choice, which returns false once the system closes the app): no drawing here.
#include <stdio.h>
#include <string.h>
#include "app_log.h"
#include "cold_code.h"
#include "emu.h"
#include "memory_cards.h"
#include "ui.h"
#include "ui_format.h"

#include "app_paths.h"

PSX3D_COLD bool cards_question(void)
{
	char label[PATH_LEN], msg[400];
	card_trouble_t t;
	while ((t = cards_trouble(label, sizeof(label))) != CARD_TROUBLE_NONE) {
		const char *name = strrchr(label, '/') ? strrchr(label, '/') + 1 : label;
		hist_entry_t copy;
		if (t == CARD_TROUBLE_DAMAGED && cards_trouble_backup(&copy)) {   // Backups has it
			char when[40];
			ui_format_when(when, sizeof(when), copy.when);
			snprintf(msg, sizeof(msg), "%.200s is damaged. A copy from %s is in Backups.", name, when);
			if (ui_confirm_choice("Memory card problem", msg, "A  Restore", "B  Other choices", false)) {
				if (cards_trouble_restore(&copy))
					emu_osd("Memory card put back from Backups");
				else
					emu_osd_alert("Could not put the memory card back. Check the SD card.");
				continue;
			}
			if (!ui_confirm_choice("Memory card problem", "Play with an empty memory card instead? "
					"The damaged file stays on the SD card as .bad.", "A  Use an empty card", "B  Quit", false)) {
				log_printf("game: quit at the memory card question\n");
				return false;
			}
			cards_accept();
			continue;
		}
		snprintf(msg, sizeof(msg), t == CARD_TROUBLE_MEMORY ?
			"%.200s could not be read: not enough memory. Try again, or play without saving to it." :
			"%.200s is damaged and has no good backup. Try again, or play with an empty card (the damaged file is kept).",
			name);
		if (ui_confirm_choice("Memory card problem", msg, "A  Try again", "B  Other choices", false)) {
			cards_retry();
			continue;
		}
		if (!ui_confirm_choice("Memory card problem", t == CARD_TROUBLE_MEMORY ?
				"Play without saving to this memory card? Nothing the game saves on it is kept this time." :
				"Play with an empty memory card? The damaged file stays on the SD card as .bad.",
				t == CARD_TROUBLE_MEMORY ? "A  Play without saving" : "A  Use an empty card", "B  Quit", false)) {
			log_printf("game: quit at the memory card question\n");
			return false;
		}
		cards_accept();
		if (t == CARD_TROUBLE_MEMORY)
			emu_osd_alert("Memory card is not saved this time (it could not be read)");
	}
	return true;
}
