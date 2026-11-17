// Shared gba_settings_ui presentation; content/state are provided by its controller.
void gba_settings_list_draw(const flist_t *list,int row,const char *scope) {
 ui_list_draw(list,gba_setting_rows(),row,gba_setting_label,gba_setting_value,NULL,theme()->accent[row&3],NULL);
 ui_list_context(scope,gba_setting_source(row));
}

