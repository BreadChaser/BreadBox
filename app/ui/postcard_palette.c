#include "postcard_palette.h"
#include "theme.h"
#include "ui_style.h"
#include "cold_code.h"

// Room themes: paper, cork and pins of the room; other themes: their own roles.
PSX3D_COLD void pc_palette_get(pc_palette *p)
{
	const theme_t *th = theme();
	const theme_room_t *r = theme_room(th);
	if (r && r->cork) {
		*p = (pc_palette){ r->paper, r->paper_edge, r->wax, r->ink, r->ink_soft, r->pin[2], theme_alpha(r->stamp, 255),
			r->cork, r->wood, r->wall, r->skirting, r->postmark, r->paper, theme_alpha(r->light, UI_PC_GLOW_ALPHA), th->shadow,
			{ r->pin[0], r->pin[1], r->pin[2], r->pin[3] }, r->paper_edge };
		if(th->look!=BB_LOOK_ROOM) { p->empty=th->text_muted; p->light=th->text_bright; p->glow=th->select_top; }
		return;
	}
	*p = (pc_palette){ th->card_top, th->card_bottom, th->light, th->heading, th->text_muted, th->accent[2], th->danger,
		th->surface, th->section_line, th->help_bg, th->surface, th->value, th->text_bright, th->select_glow, th->shadow,
		{ th->accent[0], th->accent[1], th->accent[2], th->accent[3] }, th->text_muted };
}

