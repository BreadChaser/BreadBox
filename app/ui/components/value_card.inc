/* Shared top-screen paper slip: SELECT cycles complete label/value pages at the text floor.
 * The final line carries the continuation cue only when the text needs more than one page. */
static PSX3D_COLD int value_card_lines(const char *text)
{
 return text && *text ? wrap_text(text, UI_TEXT_FLOOR, UI_VALUE_CARD_W - UI_VALUE_CARD_PAD * 2,
  0, 0, 0, 0, 0, 0) : 0;
}
PSX3D_COLD int ui_value_card_pages(const char *label, const char *value)
{
 int lines = value_card_lines(label) + value_card_lines(value);
 if (lines <= UI_VALUE_CARD_LINES) return 1;
 int capacity = UI_VALUE_CARD_LINES - 1;
 return (lines + capacity - 1) / capacity;
}
PSX3D_COLD void ui_value_card(const char *label, const char *value, int page)
{
 paper_t p;
 paper_get(&p);
 int nl = value_card_lines(label), nv = value_card_lines(value), total = nl + nv;
 if (!total) return;
 bool paged = total > UI_VALUE_CARD_LINES;
 int capacity = UI_VALUE_CARD_LINES - (paged ? 1 : 0);
 int pages = (total + capacity - 1) / capacity;
 page = page < 0 ? 0 : page % pages;
 int first = page * capacity, shown = total - first < capacity ? total - first : capacity;
 int label_first = first < nl ? first : nl;
 int label_count = nl - label_first < shown ? nl - label_first : shown;
 float h = (shown + (paged ? 1 : 0)) * UI_DIALOG_LINE + UI_VALUE_CARD_PAD * 2;
 float y = UI_VALUE_CARD_BOTTOM - h, x = UI_VALUE_CARD_X;
 float w = UI_VALUE_CARD_W - UI_VALUE_CARD_PAD * 2;
 C2D_DrawRectSolid(x + UI_CARD_SHADOW, y + UI_CARD_SHADOW, UI_CARD_Z_SHADOW, UI_VALUE_CARD_W, h, theme_alpha(theme()->shadow, UI_CRT_SHADOW_ALPHA));
 C2D_DrawRectangle(x, y, UI_CARD_Z_PAPER, UI_VALUE_CARD_W, h, p.paper, p.paper, p.edge, p.paper);
 x += UI_VALUE_CARD_PAD;
 y += UI_VALUE_CARD_PAD;
 if (label_count) wrap_text(label, UI_TEXT_FLOOR, w, x, y, UI_DIALOG_LINE, label_first, label_count, p.stamp);
 if (shown > label_count) wrap_text(value, UI_TEXT_FLOOR, w, x, y + label_count * UI_DIALOG_LINE, UI_DIALOG_LINE,
  first > nl ? first - nl : 0, shown - label_count, p.ink);
 if (paged) {
  float mark_y = y + shown * UI_DIALOG_LINE;
  float cy = mark_y + 15 * UI_TEXT_FLOOR;
  key_cue(KEY_SELECT, x, cy, p.stamp, p.paper);
  bar_icon(UI_ICON_RIGHT, x + UI_VALUE_CARD_PAGE_MARK_W - BAR_ICON_R, cy, BAR_ICON_R,
   p.stamp, p.paper, false);
  char number[32];snprintf(number, sizeof(number), "%d/%d", page + 1, pages);
  ui_text_ex(NULL, x + UI_VALUE_CARD_PAGE_MARK_W, mark_y, UI_TEXT_FLOOR, p.stamp, 0, number);
 }
}
