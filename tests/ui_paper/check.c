/* Ruled paper: every text line is a ruled line, from the layout tokens (owner 2026-11-05: "why doesn't the text
 * actually go on the lines?"). Where the letters stand on those lines with a given font (Nintendo's, Azahar's, the
 * title font) is tests/text_baseline; the rendered Settings card is checked in tests/settings_b/model.c. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "ui_style.h"
#include "ui_mark.h"
/* a y on the room card's paper is on a ruled line: the red rule, or a soft line UI_PAPER_LINE apart under it, above
 * the foot margin of a card h tall */
static int room_line(float y, float h)
{
	if (y == UI_ROOM_RULE_Y) return 1;
	float k = (y - UI_ROOM_RULE_Y) / UI_PAPER_LINE;
	return k >= 1 && k == floorf(k) && y < h - UI_ROOM_RULE_FOOT;
}
int main(void)
{
	/* one pitch: the Settings / pause card */
	assert(UI_CARD_ROW == UI_PAPER_LINE && UI_CARD_NOTICE_H == UI_PAPER_LINE);
	assert(UI_CARD_ROWS_Y == UI_CARD_RULE_Y + 1);   /* row 0's band starts under the red rule */
	for (int k = 0; k < UI_CARD_ROWS; k++) {
		float band = UI_CARD_ROWS_Y + k * UI_CARD_ROW, line = band + UI_CARD_ROW - 1;   /* as ui_card_draw */
		assert(line == UI_CARD_RULE_Y + (k + 1) * UI_PAPER_LINE);   /* the row's baseline is a ruled line */
		/* capitals (UI_PAPER_CAP) stand inside the band, above the line */
		assert(line - UI_PAPER_CAP >= band && UI_PAPER_MARK_Y(line) > band && UI_PAPER_MARK_Y(line) < line);
	}
	/* the notice's line is the last ruled line, on the card's foot */
	float notice = UI_CARD_Y + UI_CARD_H - UI_CARD_NOTICE_H + UI_CARD_NOTICE_H - 1;
	assert(notice == UI_CARD_RULE_Y + (UI_CARD_ROWS + 1) * UI_PAPER_LINE && notice < UI_CARD_Y + UI_CARD_H);
	/* the highlighter's stroke (ui_mark.h) stays clear of the line above's text and of the next row's capitals */
	assert(UI_MARK_ABOVE < UI_PAPER_LINE && UI_MARK_HEIGHT - UI_MARK_ABOVE + 1 < UI_PAPER_LINE - UI_PAPER_CAP);
	assert(UI_CARD_TEXT_X == UI_CARD_MARGIN_X + UI_PAPER_MARGIN_GAP);
	/* the room's place / catalog card and a shelf's game card (shelf_scene.c, card coordinates) */
	const float place_h = UI_ROOM_CARD_H, game_h = UI_ROOM_GAME_H;
	assert(UI_ROOM_LIST_STEP == UI_PAPER_LINE && UI_ROOM_LINE_STEP == UI_PAPER_LINE);
	/* their titles stand on the red rule in the header row, as the pause card's (ui_card_header); the body starts on
	   the first ruled line under it */
	assert(UI_ROOM_ROWS_Y == UI_ROOM_RULE_Y + UI_PAPER_LINE);
	for (float y = UI_ROOM_ROWS_Y; y <= UI_ROOM_HINT_Y; y += UI_ROOM_LIST_STEP) assert(room_line(y, place_h));
	assert(room_line(UI_ROOM_HINT_Y, place_h) && room_line(UI_ROOM_HINT_Y - UI_ROOM_LINE_STEP, place_h));
	assert(room_line(UI_ROOM_GAME_HINT_Y, game_h) && room_line(UI_ROOM_ROWS_Y, game_h));
	assert(UI_ROOM_TEXT_X == UI_ROOM_MARGIN_X + UI_PAPER_MARGIN_GAP);
	assert(UI_ROOM_STAR_UP == UI_PAPER_CAP / 2);
	/* the postcard's back: the writing on its first address line, the lines at the same pitch */
	assert(UI_PC_TOP_RULE_DY == UI_PAPER_LINE);
	/* >= 10 px capitals on the line at the floor */
	assert(UI_PAPER_CAP >= 10);
	puts("ui_paper PASS: one pitch for lines and text; card rows, notice, title, room/game card lines and the postcard write on ruled lines");
	return 0;
}
