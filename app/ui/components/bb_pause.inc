static void game_in_glass(void *unused)
{
    (void)unused;
    config.draw_game(NULL);
}

/* The shared pause top: the paused game in the CRT, its clean name on the tag (the same for every system),
 * and the SAVED postmark on the picture while a postcard has just been saved. */
void bb_pause_present_top(const char *title,const float *game_box,void (*game)(void *),const bb_pause_slots *s)
{
    char clean[96],when[24];
    ui_clean_title(title?title:"",clean,sizeof(clean));
    ui_tag_t tag={clean,{NULL},0};   /* the game's name; "Paused" and the system said what the screen shows */
    if(s && s->saved_card>=0 && svcGetSystemTick()<s->saved_until) {
        time_t now=time(NULL);
        struct tm *tm=gmtime(&now);   // the 3DS clock is local time already
        when[0]=0;
        if(tm)strftime(when,sizeof(when),"%b %d %H:%M",tm);
        ui_pause_stamp(ui_motion_ease((float)(SAVED_SECONDS*SYSCLOCK_ARM11-(s->saved_until-svcGetSystemTick())) / SYSCLOCK_ARM11 / UI_MOTION_UI_S),when);
    } else ui_pause_stamp(0.0f,NULL);
    ui_pause_scene(&tag,game_box,game,NULL);
}

/* The shared pause card: the game's clean name, the rows, a notice along the foot, the bar. */
void bb_pause_present_card(const flist_t *list,int n,int sel,void (*row)(int,void *,ui_card_row_t *),void *ud,
 const char *title,const char *notice,const ui_button_t *bar,int nbar)
{
    char clean[96];
    ui_clean_title(title?title:"",clean,sizeof(clean));
    ui_card_t card={NULL,NULL,clean,notice};
    ui_card_draw(list,n,sel,row,ud,&card);
    ui_bar_draw(bar,nbar);
}

static struct { const char *title; void (*box)(float *); void (*game)(void *); } paused_scene;
void bb_pause_scene_set(const char *title,void (*box)(float *),void (*game)(void *))
{
    paused_scene.title=title;paused_scene.box=box;paused_scene.game=game;
}
bool bb_pause_scene_top(const char *tag)
{
    if(!paused_scene.game)return false;
    float box[4];
    char clean[96];
    if(paused_scene.box)paused_scene.box(box);
    ui_clean_title(paused_scene.title?paused_scene.title:"",clean,sizeof(clean));
    ui_tag_t t={tag?tag:clean,{NULL},0};
    ui_pause_stamp(0.0f,NULL);
    ui_pause_scene(&t,paused_scene.box?box:NULL,paused_scene.game,NULL);
    return true;
}

static void draw_paused_top(void *unused)
{
    (void)unused;
    float box[4];
    if(config.system->picture_box)config.system->picture_box(box);
    bb_pause_present_top(menu_title,config.system->picture_box?box:NULL,game_in_glass,&slots_now);
}

static void draw_pause(void *unused)
{
    (void)unused;
    bb_pause_present_card(&menu_list,pause_count(),menu_row,card_row,NULL,menu_title,svcGetSystemTick()<notice_until?notice:NULL,pause_bar,PAUSE_BAR);
}

static void draw_confirm(void *ud) {
    bb_confirm_t *c=ud;
    ui_dialog_draw(c->title,NULL,c->text,c->bar,2);
}

/* The Continue question (bb_pause.h): the Continue picture in the CRT, its date on the tag; the same card and
 * bar for every system. */
typedef struct { C3D_Tex *tex; float tw, th; } continue_picture;
static void continue_in_glass(void *ud)
{
    const continue_picture *c=ud;
    if(!c->tex)return;
    const Tex3DS_SubTexture sub={(u16)c->tw,(u16)c->th,0.0f,1.0f,c->tw/c->tex->width,1.0f-c->th/c->tex->height};
    C2D_DrawImageAt((C2D_Image){c->tex,&sub},0,0,UI_CRT_Z_GLASS+0.01f,NULL,UI_TOP_SCREEN_WIDTH/c->tw,UI_SCREEN_HEIGHT/c->th);
}
void bb_continue_present_top(const char *title,C3D_Tex *tex,float tw,float th,u32 saved)
{
    char clean[96],when[40];
    ui_clean_title(title?title:"",clean,sizeof(clean));
    when[0]=0;
    if(saved) {
        time_t t=(time_t)saved;
        struct tm *tm=gmtime(&t);   // the 3DS clock is local time already
        if(tm)strftime(when,sizeof(when),"%b %d %H:%M",tm);
    }
    /* "Left off" over the date: one line was cut on the tag */
    ui_tag_t tag={clean,{when[0]?"Left off":NULL,when[0]?when:NULL,NULL},0};
    continue_picture c={tex&&tw>0.0f?tex:NULL,tw,th};
    ui_pause_stamp(0.0f,NULL);
    ui_pause_scene(&tag,NULL,continue_in_glass,&c);
}
void bb_continue_present_card(const char *title,C3D_Tex *tex,float tw,float th,u32 saved,const char *stamp,bool older,bool over_failed)
{
    char clean[96];
    ui_clean_title(title?title:"",clean,sizeof(clean));
    const pc_continue card={tex&&tw>0.0f?tex:NULL,tw,th,0.0f,clean,stamp,saved,over_failed?UI_CONTINUE_OVER_FAILED:NULL,true,false};
    pc_continue_card(&card);
    ui_bar_draw(bb_continue_bar,BB_CONTINUE_BUTTONS(older));
}
static void draw_continue_top(void *unused)
{
    (void)unused;
    bb_continue_present_top(menu_title,continue_view.card.tex,continue_view.card.tw,continue_view.card.th,continue_view.saved);
}
static void draw_continue(void *over_failed) {
    bb_continue_present_card(menu_title,continue_view.card.tex,continue_view.card.tw,continue_view.card.th,continue_view.saved,
        config.system?config.system->short_name:NULL,false,*(const bool *)over_failed);
}

static void draw_settings(void *unused)
{
 (void)unused;
 C2D_DrawRectSolid(0,0,UI_Z_BACKDROP,UI_BOTTOM_SCREEN_WIDTH,UI_SCREEN_HEIGHT,theme()->surface);
 ui_list_context("Settings",config.system->setting_source?config.system->setting_source(setting_row):ui_fit(menu_title,UI_FONT_META,150));
 ui_list_draw(&settings_list,config.system->nsettings,setting_row,setting_label,setting_value,NULL,theme()->accent[setting_row&3],NULL);
 ui_bar_draw(settings_bar,4);
}
