// Shared share presentation; content/state are provided by its controller.
static void share_tag(void *ud)
{
 (void)ud;
 static const char *const hints[][UI_TAG_LINES] = {
  { "Official release", "Needs internet", "A QR · B Back" },
  { "From this 3DS", "Needs BreadBox.cia", "Same Wi-Fi" },
  { "FBI: Remote Install", "Scan QR Code", "A Steps · B Back" },
  { "Contributors", "and licenses", "A Open · B Back" },
 };
 ui_tag_t tag = { .title = "BreadBox for a friend", .hot = -1 };
 for (int i = 0; i < UI_TAG_LINES; i++) tag.line[i] = hints[share_item()][i];
 if (share_qr) {
  tag.line[0] = "FBI: Remote Install";
  tag.line[1] = "Scan QR Code";
  tag.line[2] = "B Back";
 } else if (share_item() == SHARE_LOCAL && serving) {
  tag.line[0] = "Sharing this file";
  tag.line[1] = "Same Wi-Fi only";
  tag.line[2] = "A QR code · X Stop";
 }
  ui_tag(&tag, UI_TAG_X, UI_TAG_Y);
  if (share_item() == SHARE_LOCAL)
   ui_help_panel(138, serving ? "Sharing this file" : "Local sharing", serving ? url : share_failed ? cia_note : NULL, serving ? cia_note :
       "Put BreadBox.cia in sdmc:/cias/. The installed app is not a shareable file. Both systems must use the same Wi-Fi.");
}
// The list's item in focus, on the top screen
static void draw_top(void *ud)
{
	(void)ud;
 if (share_scene && share_scene->draw_top) {
  ui_destination_draw_top(share_scene, share_tag, NULL);
  return;
 }
	const theme_t *th = theme();
	u32 white = th->heading, dim = th->text_muted;
	scene_draw(false);
	ui_companion_title("Share " APP_NAME, PSX3D_VERSION);
	switch (share_item()) {
	case SHARE_RELEASE:
		ui_text(UI_SHARE_LEFT, 42, UI_FONT_BODY, white, "Official release (needs internet):");
		ui_text(UI_SHARE_LEFT, 70, UI_FONT_VALUE, dim, "github.com/BreadChaser/" APP_NAME "/releases");
		ui_text(UI_SHARE_LEFT, 110, UI_FONT_META, dim, "A shows its QR code below.");
		break;
	case SHARE_LOCAL:
		ui_text(UI_SHARE_LEFT, 42, UI_FONT_BODY, white, "From this 3DS (same Wi-Fi only):");
		if (serving) {
			ui_text(UI_SHARE_LEFT, 70, UI_FONT_VALUE, dim, "%s", url);
			ui_text(UI_SHARE_LEFT, 92, UI_FONT_META, dim, "%s   Sent: %lu", cia_note, served_count);
			ui_text(UI_SHARE_LEFT, 110, UI_FONT_META, dim, "A shows its QR code below. X stops sharing.");
		} else {
			ui_text(UI_SHARE_LEFT, 70, UI_FONT_VALUE, dim, "Needs BreadBox.cia on the SD card.");
			ui_text(UI_SHARE_LEFT, 110, UI_FONT_META, share_failed ? th->message : dim, "%s", share_failed ? cia_note : "Put BreadBox.cia in sdmc:/cias/. Same Wi-Fi.");
		}
		break;
	case SHARE_INSTALL:
		ui_text(UI_SHARE_LEFT, 42, UI_FONT_BODY, white, "Share " APP_NAME " with a friend:");
		ui_text(UI_SHARE_LEFT, 70, UI_FONT_VALUE, dim, "On their 3DS open FBI > Remote Install >");
		ui_text(UI_SHARE_LEFT, 92, UI_FONT_VALUE, dim, "Scan QR Code, and scan the code here.");
		ui_text(UI_SHARE_LEFT, 110, UI_FONT_META, dim, "A shows the steps.");
		break;
	default:
		ui_text(UI_SHARE_LEFT, 42, UI_FONT_VALUE, dim, "Multi-system emulator for 3DS. Free software (GPL).");
		ui_text(UI_SHARE_LEFT, 70, .38f, dim, "Icons by Dreamstale from Flaticon");
		ui_text(UI_SHARE_LEFT, 92, .38f, dim, "Hand model: 'Hand (low poly)' by scribbletoad, CC BY 4.0, modified");
		ui_text(UI_SHARE_LEFT, 110, UI_FONT_META, dim, "A shows the credits.");
		break;
	}
}
static void draw_qr(void);
static void share_row(int i, void *ud, ui_card_row_t *row)
{
 *row = (ui_card_row_t){ .label = share_label(i, ud), .value = share_value(i, ud), .icon = UI_ICON_OPEN };
}
// Shared card content, or an unanimated full-screen scanning surface.
static void draw_bottom(void *ud)
{
 (void)ud;
 if (share_qr) { draw_qr(); return; }
 ui_card_t card = { .eyebrow = "Share", .eyebrow_right = in_game ? "About" : "Coffee table",
  .title = "BreadBox for a friend", .notice = share_start_pending ? "Checking BreadBox.cia on the SD card" : serving ? "X Stop sharing" : share_failed ? "Sharing unavailable. See the help tag." : NULL };
 ui_card_draw(&share_list, share_n, share_sel, share_row, NULL, &card);
 static const ui_button_t bar[] = { { KEY_B, "Back", UI_ICON_BACK, false }, { KEY_A, "Select", UI_ICON_OPEN, false } };
 ui_bar_draw(bar, 2);
}
static void draw_qr(void)
{
	C2D_DrawRectSolid(0, 0, UI_Z_BACKDROP, UI_BOTTOM_SCREEN_WIDTH, UI_SCREEN_HEIGHT, UI_COLOR_QR_WHITE);   // fixed: the QR code scans black on white
	if (!qr_size)
		return;
	float px = (float)(int)(228.0f / (qr_size + 8));
	float x0 = (UI_BOTTOM_SCREEN_WIDTH - px * qr_size) / 2, y0 = (UI_SCREEN_HEIGHT - px * qr_size) / 2;
	u32 black = UI_COLOR_QR_BLACK;   // fixed: QR modules
	for (int y = 0; y < qr_size; y++)
		for (int x = 0; x < qr_size;) {
			if (!qr[y * qr_size + x]) {
				x++;
				continue;
			}
			int run = 1;
			while (x + run < qr_size && qr[y * qr_size + x + run])
				run++;
			C2D_DrawRectSolid(x0 + x * px, y0 + y * px, .5f, run * px, px, black);
			x += run;
		}
}
