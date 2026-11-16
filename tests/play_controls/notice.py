"""Execute real notice/text drawing against a depth-recording renderer."""
from pathlib import Path
import subprocess,tempfile,os
r=Path(__file__).resolve().parents[2]
s=(r/'app/ui/ui.c').read_text()
def body(name):
 a=s.index('void '+name+'(');b=s.index('\n}',a)+2;return s[a:b]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "ui_style.h"
typedef uint32_t u32;
typedef void *C2D_Font;
typedef struct {int unused;} C2D_Text;
static void *textbuf;
#define C2D_WithColor 1
static struct {u32 osd_bg,notice,text;} g_game_palette;
static float rect_z,text_z,rect_x,rect_w,text_x;
static const char *painted;
static void C2D_DrawRectSolid(float x,float y,float z,float w,float h,u32 c){(void)c;assert(y==178 && h==22);rect_x=x;rect_z=z;rect_w=w;}
static bool C2D_TextFontParse(C2D_Text *t,C2D_Font f,void *b,const char *s){(void)t;(void)f;(void)b;painted=s;return true;}
static void C2D_TextOptimize(C2D_Text *t){(void)t;}
static void C2D_DrawText(C2D_Text *t,u32 f,float x,float y,float z,float a,float b,u32 c){(void)t;(void)f;(void)y;(void)a;(void)b;(void)c;text_z=z;text_x=x;}
static const char *ui_fit(const char *s,float size,float width){(void)size;assert(width==312);return s;}
'''
source=prefix+body('ui_text_ex')+'\n'+body('ui_game_notice')+r'''
int main(void){for(int alert=0;alert<2;alert++){ui_game_notice("Visible notice",alert);if(rect_z>=text_z)return 1;assert(rect_x==(400-rect_w)/2 && text_x==rect_x+4);assert(!strcmp(painted,"Visible notice"));}puts("PASS: real notice text above background and centered on top screen");}
'''
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
 p=Path(tmp);(p/'notice.c').write_text(source)
 subprocess.run(['cc','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(r/'include/breadbox'),str(p/'notice.c'),'-o',str(p/'notice')],check=True)
 subprocess.run([str(p/'notice')],check=True)
 # The actual merge regression must be caught by this draw test.
 (p/'notice.c').write_text(source.replace('178,UI_Z_NOTICE','178,.9f'))
 subprocess.run(['cc','-std=gnu11','-I'+str(r/'include/breadbox'),str(p/'notice.c'),'-o',str(p/'bad')],check=True)
 assert subprocess.run([str(p/'bad')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode,'bad background depth escaped'
print('PASS: depth regression negative control')
