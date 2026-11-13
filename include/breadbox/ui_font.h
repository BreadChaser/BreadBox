#pragma once
// Where the loaded fonts' letters stand (app/ui/ui_font.c). citro2d draws every font in a 30 px cell from the y it
// is given; the baseline inside that cell belongs to the font: Nintendo's system font (per region), an emulator's
// replacement, the romfs title font. ui_style.h's UI_PAPER_TOP / UI_FONT_BASELINE read it from here.
// No citro2d types: the style tokens include this from host tests too.
enum { UI_FACE_SYSTEM, UI_FACE_TITLE };
// The face's baseline in its 30 px cell: the bottom of its flat letters (measured from the glyph sheet at first use,
// else the font's own baselinePos).
float ui_face_baseline(int face);
// Write the measured metrics of both faces to the log (dev switch only; dev_init).
void ui_font_log(void);
