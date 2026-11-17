#include "look_style.h"
#include "theme.h"
#include "ui_style.h"
#include "libui.h"
#include "cold_code.h"
#define RGB(r,g,b) TH_RGB(r,g,b)
static const bb_look_style styles[]={
 {RGB(250,207,82),RGB(255,109,149),RGB(88,224,164),
  {RGB(255,104,136),RGB(250,207,82),RGB(88,224,164),RGB(106,167,255)},RGB(196,209,255)},
 {RGB(210,212,216),RGB(210,212,216),RGB(185,189,197),
  {RGB(140,145,154),RGB(140,145,154),RGB(140,145,154),RGB(140,145,154)},RGB(210,212,216)}
};
PSX3D_COLD const bb_look_style *look_style(void)
{return &styles[theme_look()==BB_LOOK_SIMPLE];}
#include "components/look_style.inc"
