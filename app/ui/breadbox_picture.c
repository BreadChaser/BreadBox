// The last game's picture for the room's TV (last_picture.h bb_last_picture_*): a
// system without a Continue thumbnail of its own (GBA) hands its last frame
// over when the game ends; the main menu shows it on the TV. One small file,
// rewritten at every quit. PS1 keeps its auto-save thumbnail (savestates.c).
#include "app_policy.h"
#include <math.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "safefile.h"
#include "app_paths.h"
#include "app_log.h"
#include "last_picture.h"
#include "safefile.h"
#include "bb_save.h"

#define PICTURE_FILE PSX3D_SAVE_DIR "/last-picture.thm"
#define PICTURE_MAGIC 0x504c4242u   // "BBLP"

typedef struct { u32 magic, w, h, saved; char path[256]; } picture_head;

// The newest picture stays in memory (the TV takes it from here at once); its file is
// written after the menu is back (bb_later: 0.25 s New, 0.5 s Old on the way out of a game).
typedef struct { picture_head head; u16 px[BB_PICTURE_W * BB_PICTURE_H]; } picture_file;
static picture_file *recent;
static void write_file(const picture_file *p)
{
	FILE *f = safe_begin(PICTURE_FILE);
	bool ok = f && fwrite(p, sizeof(*p), 1, f) == 1;
	if (f) ok = safe_finish(f, PICTURE_FILE, ok);
	log_printf("last picture: %s\n", ok ? "written" : "not written");
}
static void write_copy(void *arg) { write_file(arg); free(arg); }   // the later thread's own copy
// a game starts: its memory goes back (the file is there; bb_save_settle has run)
void bb_last_picture_release(void)
{
	free(recent);
	recent = NULL;
}

// Fit w x h (RGB565, pitch in pixels) into 256 x 192, aspect kept, black bars;
// each pixel averages 2x2 samples of its source area.
void bb_last_picture_save(const char *game_path, const void *pixels, int w, int h, int pitch)
{
	if (!game_path || !pixels || w <= 0 || h <= 0) return;
	if (!recent) recent = malloc(sizeof(*recent));
	if (!recent) return;
	u16 *out = recent->px;
	memset(out, 0, sizeof(recent->px));
	const u16 *src = pixels;
	float scale = fminf((float)BB_PICTURE_W / w, (float)BB_PICTURE_H / h);
	int ow = (int)(w * scale), oh = (int)(h * scale), ox = (BB_PICTURE_W - ow) / 2, oy = (BB_PICTURE_H - oh) / 2;
	for (int y = 0; y < oh; y++) {
		int sy0 = (int)((y + .25f) / scale), sy1 = (int)((y + .75f) / scale);
		if (sy1 >= h) sy1 = h - 1;
		for (int x = 0; x < ow; x++) {
			int sx0 = (int)((x + .25f) / scale), sx1 = (int)((x + .75f) / scale);
			if (sx1 >= w) sx1 = w - 1;
			u16 p[4] = { src[sy0 * pitch + sx0], src[sy0 * pitch + sx1], src[sy1 * pitch + sx0], src[sy1 * pitch + sx1] };
			u32 r = 0, g = 0, b = 0;
			for (int i = 0; i < 4; i++) { r += p[i] >> 11; g += (p[i] >> 5) & 63; b += p[i] & 31; }
			out[(oy + y) * BB_PICTURE_W + ox + x] = (u16)((r / 4) << 11 | (g / 4) << 5 | b / 4);
		}
	}
	recent->head = (picture_head){ PICTURE_MAGIC, BB_PICTURE_W, BB_PICTURE_H, (u32)time(NULL), "" };
	snprintf(recent->head.path, sizeof(recent->head.path), "%s", game_path);
	picture_file *copy = malloc(sizeof(*copy));
	if (!copy) { write_file(recent); return; }
	memcpy(copy, recent, sizeof(*copy));
	bb_later(write_copy, copy);
}

// The top screen's framebuffer (RGB8, 240 x 400 turned a quarter, BGR bytes) still
// holds the game's last picture when its run() returns: crop it to the lit area
// (the game picture inside its black margins) and keep that.
void bb_last_picture_from_screen(const char *game_path)
{
	u16 fw, fh;
	const u8 *fb = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fw, &fh);   // fw = 240 screen rows, fh = 400 columns
	if (!fb || fw != 240 || fh < 400) return;
	GSPGPU_InvalidateDataCache(fb, fw * fh * 3);
	u16 *img = malloc(400 * 240 * 2);
	if (!img) return;
	int x0 = 400, x1 = -1, y0 = 240, y1 = -1;
	for (int sx = 0; sx < 400; sx++)
		for (int sy = 0; sy < 240; sy++) {
			const u8 *p = fb + (sx * fw + (fw - 1 - sy)) * 3;
			img[sy * 400 + sx] = (u16)((p[2] >> 3) << 11 | (p[1] >> 2) << 5 | p[0] >> 3);
			if (p[0] + p[1] + p[2] > 24) {
				if (sx < x0) x0 = sx;
				if (sx > x1) x1 = sx;
				if (sy < y0) y0 = sy;
				if (sy > y1) y1 = sy;
			}
		}
	if (x1 - x0 >= 64 && y1 - y0 >= 48)
		bb_last_picture_save(game_path, img + y0 * 400 + x0, x1 - x0 + 1, y1 - y0 + 1, 400);
	free(img);
}

bool bb_last_picture_read(const char *game_path,u16 *px,bool (*cancel)(void *),void *arg)
{
 if(cancel && cancel(arg))return false;
 if(recent) {
  bool ok=!game_path || !strncmp(recent->head.path,game_path,sizeof(recent->head.path)-1);
  if(ok)memcpy(px,recent->px,sizeof(recent->px));
  return ok;
 }
 // Snapshot readers never rename or delete a copy being written by bb_later.
 static const char *const paths[]={PICTURE_FILE,PICTURE_FILE ".bak"};
 for(unsigned i=0;i<2;i++) {
  if(cancel && cancel(arg))return false;
  FILE *f=fopen(paths[i],"rb");if(!f)continue;
  picture_head head;
  bool ok=!(cancel && cancel(arg)) && fread(&head,sizeof(head),1,f)==1 &&
   head.magic==PICTURE_MAGIC && head.w==BB_PICTURE_W && head.h==BB_PICTURE_H &&
   (!game_path || !strncmp(head.path,game_path,sizeof(head.path)-1));
  for(size_t off=0;ok && off<BB_PICTURE_W*BB_PICTURE_H*2;off+=4096) {
   size_t n=BB_PICTURE_W*BB_PICTURE_H*2-off;if(n>4096)n=4096;
   ok=!(cancel && cancel(arg)) && fread((u8 *)px+off,1,n,f)==n;
  }
  fclose(f);if(ok)return true;
 }
 return false;
}
bool bb_last_picture_load(const char *game_path,u16 *px)
{
 return bb_last_picture_read(game_path,px,NULL,NULL);
}
