/* What the frozen v1-r2 settings fixture calls that production no longer has. Include it before the fixture.
 * ui_list_input_sections: the list input that skips section headings (app/ui/ui.c until 2026-11-06; its last
 * production user was the 2D game library). The fixture's Settings pages still use it, so the test keeps its copy. */
#ifndef V1_R2_RETIRED_H
#define V1_R2_RETIRED_H
static u32 ui_list_input_sections(const flist_t *f,int n,int *sel,u32 keys,bool horizontal,
 bool (*heading)(int,void *),void *ud)
{
 int before=*sel;
 keys=ui_list_input(f,n,sel,keys,horizontal);
 if(heading && n>UI_METRIC_0 && heading(*sel,ud)) {
  if(keys&KEY_TOUCH) {*sel=before;keys&=~KEY_A;}
  else {
   int dir=*sel<before?-UI_METRIC_1:UI_METRIC_1,row=*sel;
   while(row>=UI_METRIC_0 && row<n && heading(row,ud))row+=dir;
   *sel=row>=UI_METRIC_0 && row<n?row:before;
  }
 }
 return keys;
}
#endif
