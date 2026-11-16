/* Shared update preference card; all geometry belongs to ui_card_draw. */
static void update_bottom(void *unused)
{
 (void)unused;ui_card_t card={.eyebrow="SETTINGS",.eyebrow_right=BB_VERSION,.title="Updates",.notice=update_card_notice()};if(!update_line_bad)ui_card_notice_calm();
 ui_card_draw(&update_list,BB_UPDATE_ROWS,update_selected,update_row,NULL,&card);
 ui_button_t bar[2];ui_bar_draw(bar,update_bar(bar));
}

static void update_progress_bottom(void *unused)
{
 (void)unused;
 ui_progress_card_draw("Updating BreadBox...","Keep power on. HOME interrupts the update.",__atomic_load_n(&update_progress,__ATOMIC_ACQUIRE));
}
