/* Screen-depth information, clear of the room picture's middle. */
static PSX3D_COLD void notice_bar_draw(const char *s,int alpha)
{
 C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
 float size=UI_CARD_ROW_SIZE;
 C2D_DrawRectSolid(UI_NOTICE_LEFT,UI_NOTICE_TOP,UI_Z_NOTICE,UI_TOP_SCREEN_WIDTH,UI_NOTICE_HEIGHT,
  theme_alpha(ui_bar_color(UI_COL_NOTICE),alpha));
 C2D_DrawRectSolid(UI_NOTICE_LEFT,UI_NOTICE_TOP,UI_Z_NOTICE,UI_TOP_SCREEN_WIDTH,UI_NOTICE_RULE,
  theme_alpha(theme()->accent[UI_NOTICE_ACCENT],alpha));
 ui_text_ex(NULL,UI_NOTICE_INSET,UI_NOTICE_TOP+UI_NOTICE_TEXT_INSET,size,
  theme_alpha(theme()->heading,alpha),UI_TEXT_ALIGN_LEFT,ui_fit(s,size,UI_NOTICE_TEXT_WIDTH));
 C2D_Flush();C3D_DepthTest(true,GPU_GEQUAL,GPU_WRITE_ALL);
}
