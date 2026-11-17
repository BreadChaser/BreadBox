// Shared library presentation; content/state are provided by its controller.
static void draw_index_progress(void *unused)
{
    (void)unused;
    char counts[96];
    bb_system_counts(counts,sizeof(counts));
    ui_progress_draw(index_wait_slow?"Still checking...":"Finding your games",index_wait_slow?"B to skip":counts,-1);
    ui_text_wrap(UI_WRAP_X,150,UI_FONT_META,theme()->help_text,UI_WRAP_W,bb_index_root());
    ui_button_t bar[]={{KEY_B,index_wait_slow?"Skip":"Continue",UI_ICON_PLAY,false}};
    ui_bar_draw(bar,1);
}
