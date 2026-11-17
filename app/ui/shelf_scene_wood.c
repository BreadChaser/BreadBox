#include "shelf_scene_internal.h"

const short shelf_scene_grain_region[G_COUNT][4] = {
	{ 0, 0, 512, 64 }, { 0, 64, 512, 32 }, { 0, 96, 512, 32 }, { 0, 128, 256, 256 }, { 256, 128, 64, 256 },
	{ 320, 128, 192, 128 }, { 320, 256, 192, 128 }, { 0, 384, 256, 128 }, { 256, 384, 128, 32 } };

// ---------------------------------------------------------------- grain (the mock's procedural wood)
static float ih(int x, int y, int s)
{
	u32 h = (u32)x * 374761393u + (u32)y * 668265263u + (u32)s * 1442695041u;
	h = (h ^ (h >> 13)) * 1274126177u;
	h ^= h >> 16;
	return h / 4294967296.0f;
}
// Value noise. The grain walks texel by texel, so each call site keeps the
// lattice cell it used last and its four corner values.
typedef struct { int xi, yi, s; float a, b, c, d; } cell_t;
static float vnc(float x, float y, int s, cell_t *k)
{
	int xi = x >= 0 ? (int)x : (int)floorf(x), yi = y >= 0 ? (int)y : (int)floorf(y);
	if (xi != k->xi || yi != k->yi || s != k->s) {
		*k = (cell_t){ xi, yi, s, ih(xi, yi, s), ih(xi + 1, yi, s), ih(xi, yi + 1, s), ih(xi + 1, yi + 1, s) };
	}
	float xf = x - xi, yf = y - yi, u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
	return k->a + (k->b - k->a) * u + (k->c - k->a) * v + (k->a - k->b - k->c + k->d) * u * v;
}
static cell_t noise_cell[5];
static void noise_reset(void) { for (int i = 0; i < 5; i++) noise_cell[i] = (cell_t){ .xi = 1 << 30 }; }
static float vn(float x, float y, int s) { return vnc(x, y, s, &noise_cell[3]); }
static float fbm(float x, float y, int s)
{
	return vnc(x, y, s, &noise_cell[0]) * .6f + vnc(x * 2.1f, y * 2.1f, s + 7, &noise_cell[1]) * .28f + vnc(x * 4.3f, y * 4.3f, s + 13, &noise_cell[2]) * .12f;
}

typedef struct { bool along_x; int seed, knots, planks; float ring, light, dark; bool edge, nails, scratch; } wood_t;
// ring^2.6 for ring in [0, 1], sampled (the grain is made once, then cached)
static float ring_pow[257];
static float ringp(float r) { float f = r * 256; int i = (int)f; return ring_pow[i] + (ring_pow[i + 1 < 257 ? i + 1 : 256] - ring_pow[i]) * (f - i); }
// Grey value: the mock's lerp(light, dark, ring) * tone, normalised so the
// room's material colour is the wood's lightest tone.
typedef struct { float knot[6][3]; } wood_state;
static void wood(u8 *dst, const short *rg, const wood_t *o, int first, int end, wood_state *state)
{
	int w = rg[2], h = rg[3];
	float (*knot)[3]=state->knot;
	if(!first)
	for (int i = 0; i < o->knots; i++) {
		knot[i][0] = rnd(o->seed * 31 + i) * (o->along_x ? w : h);
		knot[i][1] = rnd(o->seed * 17 + i + 5) * (o->along_x ? h : w);
		knot[i][2] = 5 + rnd(o->seed + i * 3) * 7;
	}
	if (!ring_pow[256]) for (int i = 0; i <= 256; i++) ring_pow[i] = powf(i / 256.0f, 2.6f);
	// sin(a * .012 + pl * 2.1) = sin(A) cos(B) + cos(A) sin(B), A per texel line, B per plank
	static float sa[512], ca[512], sp[8], cp[8];
	static bool trig_ready;
	if(!trig_ready) {
		for (int i = 0; i < 512; i++) { sa[i] = sinf(i * .012f); ca[i] = cosf(i * .012f); }
		for (int i = 0; i < 8; i++) { sp[i] = sinf(i * 2.1f); cp[i] = cosf(i * 2.1f); }
		trig_ready=true;
	}
	int span = o->along_x ? h : w, planks = o->planks ? o->planks : 1;
	for (int y = first; y < h && y < end && !bb_index_cancelled(); y++)
		for (int x = 0; x < w; x++) {
			int ai = o->along_x ? x : y;
			float a = ai, b = o->along_x ? y : x;
			int pl = (int)(b / span * planks);
			float pb = b - pl * (float)span / planks;
			float g = pb * o->ring + 9 * fbm(a * .004f, pb * .03f, o->seed + pl) + 1.6f * (sa[ai] * cp[pl] + ca[ai] * sp[pl]);
			float kn = 1;
			for (int i = 0; i < o->knots; i++) {
				float da = (a - knot[i][0]) * .42f, db = b - knot[i][1], d2 = da * da + db * db, r2 = knot[i][2] * knot[i][2];
				if (d2 < r2 * 36) g += 7 * expf(-d2 / (r2 * 3));   // beyond: below 1/100000
				if (d2 < r2 * .25f) g += 4;
				if (d2 < r2 * .16f) kn = .55f;
			}
			float ring = g - floorf(g), t = ringp(ring) * .75f + .25f * vn(a * .03f, b * .9f, o->seed + 3);
			float frac = pb / ((float)span / planks) * 2 - 1, f2 = frac * frac, f4 = f2 * f2;
			float k = (.9f + .2f * ih(pl, 0, o->seed)) * (1 - .25f * f4 * f4);
			if (planks > 1 && pb < 1.4f) k *= .45f;
			if (o->scratch && ih((int)(a / 3), (int)(b * .5f), o->seed + 9) > .997f) k *= 1.25f;
			float v = lerpf(1, o->dark, t) * k * kn;
			if (o->edge) {
				float fy = (float)y / h;
				v *= fy < .12f ? lerpf(1.38f, 1.08f, fy / .12f) : fy < .2f ? lerpf(1.08f, 1, (fy - .12f) / .08f) : fy > .85f ? lerpf(1, .5f, (fy - .85f) / .15f) : 1;
			}
			dst[texel(rg[0] + x, rg[1] + y, GRAIN_W)] = (u8)clampf(v * 230, 0, 255);
		}
	if (o->nails && end >= h && !bb_index_cancelled())
		for (int p = 0; p < planks; p++)
			for (int j = 0; j < 2; j++) {
				int cx = (int)((p + .5f) * w / planks), cy = (int)((j ? .92f : .08f) * h);
				for (int dy = -2; dy <= 2; dy++)
					for (int dx = -2; dx <= 2; dx++)
						if (dx * dx + dy * dy <= 5) dst[texel(rg[0] + cx + dx, rg[1] + cy + dy, GRAIN_W)] = dx < 0 && dy < 0 ? 140 : 30;
			}
}
static void plaster(u8 *dst, const short *rg, int first, int end)
{
	for (int y = first; y < rg[3] && y < end && !bb_index_cancelled(); y++)
		for (int x = 0; x < rg[2]; x++) {
			float n = fbm(x * .03f, y * .03f, 41) * .7f + vn(x * .4f, y * .4f, 42) * .3f;
			dst[texel(rg[0] + x, rg[1] + y, GRAIN_W)] = (u8)clampf((n * .7f + .3f) * 255, 0, 255);
		}
}
// Brass name plate: a vertical sheen and an engraved inner border.
static void plate(u8 *dst, const short *rg, int first, int end)
{
	for (int y = first; y < rg[3] && y < end && !bb_index_cancelled(); y++)
		for (int x = 0; x < rg[2]; x++) {
			float fy = (float)y / rg[3], v = fy < .45f ? lerpf(1, .72f, fy / .45f) : lerpf(.72f, .44f, (fy - .45f) / .55f);
			bool border = (x == 3 || x == rg[2] - 4 || y == 3 || y == rg[3] - 4) && x >= 3 && x <= rg[2] - 4 && y >= 3 && y <= rg[3] - 4;
			bool edge = x == 0 || y == 0 || x == rg[2] - 1 || y == rg[3] - 1;
			if (border) v *= .6f;
			if (edge) v *= .5f;
			dst[texel(rg[0] + x, rg[1] + y, GRAIN_W)] = (u8)clampf(v * 255, 0, 255);
		}
}
static void make_grain_part(u8 *g,int part,int first,int end,wood_state *state)
{
	if(!part && !first)noise_reset();
	static const wood_t board = { true, 3, 4, 1, .08f, 1, 96.f / 176, false, false, true };
	static const wood_t lip = { true, 5, 2, 1, .1f, 1, 82.f / 150, true, false, true };
	static const wood_t crown = { true, 61, 3, 1, .07f, 1, 78.f / 150, true, false, false };
	static const wood_t back = { false, 7, 5, 6, .06f, 1, 54.f / 104, false, true, false };
	static const wood_t upright = { false, 9, 2, 1, .09f, 1, 86.f / 160, false, false, true };
	static const wood_t floor_ = { true, 13, 3, 5, .05f, 1, 56.f / 110, false, false, false };
	static const wood_t desk = { true, 17, 2, 3, .07f, 1, 44.f / 96, false, false, true };
	if(part==0)wood(g, shelf_scene_grain_region[G_BOARD], &board, first, end, state);
	if(part==1)wood(g, shelf_scene_grain_region[G_LIP], &lip, first, end, state);
	if(part==2)wood(g, shelf_scene_grain_region[G_CROWN], &crown, first, end, state);
	if(part==3)wood(g, shelf_scene_grain_region[G_BACK], &back, first, end, state);
	if(part==4)wood(g, shelf_scene_grain_region[G_UPRIGHT], &upright, first, end, state);
	if(part==5)wood(g, shelf_scene_grain_region[G_FLOOR], &floor_, first, end, state);
	if(part==6)plaster(g, shelf_scene_grain_region[G_WALL], first, end);
	if(part==7)wood(g, shelf_scene_grain_region[G_DESK], &desk, first, end, state);
	if(part==8)plate(g, shelf_scene_grain_region[G_PLATE], first, end);
}

/* Missing grain used to generate all 262144 texels and write the SD cache in
 * the first Library frame (~714 ms in Azahar). Worker-owned bytes outlive the
 * GPU texture; two rows per low-lane job, cancellable between rows. */
static struct {u8 *buf;size_t size;int state,part,row;bool ready;wood_state wood;} grain_job;
static void grain_work(void *unused)
{
 (void)unused;
 if(bb_index_cancelled()){__atomic_store_n(&grain_job.state,0,__ATOMIC_RELEASE);return;}
 u64 start=svcGetSystemTick();
 if(!grain_job.part && !grain_job.row) {
  FILE *f=fopen(GRAIN_FILE,"rb");bool ok=f!=NULL;
  for(size_t at=0;ok && at<grain_job.size;at+=65536) {
   size_t n=grain_job.size-at<65536?grain_job.size-at:65536;
   ok=!bb_index_cancelled() && fread(grain_job.buf+at,1,n,f)==n;
  }
  if(f)fclose(f);
  if(ok){grain_job.ready=true;__atomic_store_n(&grain_job.state,2,__ATOMIC_RELEASE);return;}
  if(bb_index_cancelled()){__atomic_store_n(&grain_job.state,0,__ATOMIC_RELEASE);return;}
  memset(grain_job.buf,128,grain_job.size);
 }
 if(grain_job.part<G_COUNT) {
  make_grain_part(grain_job.buf,grain_job.part,grain_job.row,grain_job.row+2,&grain_job.wood);
  if(bb_index_cancelled()){__atomic_store_n(&grain_job.state,0,__ATOMIC_RELEASE);return;}
  grain_job.row+=2;
  if(grain_job.row>=shelf_scene_grain_region[grain_job.part][3]){grain_job.part++;grain_job.row=0;}
 }
 if(grain_job.part==G_COUNT && !bb_index_cancelled()) {
  mkdir(PSX3D_DIR "/cache",0777);
  FILE *f=fopen(GRAIN_FILE ".tmp","wb");
  if(f){bool ok=true;for(size_t at=0;ok && at<grain_job.size;at+=16384){size_t n=grain_job.size-at<16384?grain_job.size-at:16384;ok=!bb_index_cancelled() && fwrite(grain_job.buf+at,1,n,f)==n;}fclose(f);if(ok)rename(GRAIN_FILE ".tmp",GRAIN_FILE);else remove(GRAIN_FILE ".tmp");}
  grain_job.ready=true;
 }
 if(grain_job.row==0)log_printf("shelf: grain worker part=%d chunk-ms=%lu\n",grain_job.part,(unsigned long)((svcGetSystemTick()-start)/(SYSCLOCK_ARM11/1000)));
 __atomic_store_n(&grain_job.state,grain_job.ready?2:0,__ATOMIC_RELEASE);
}
void shelf_scene_grain_step(void)
{
 int state=__atomic_load_n(&grain_job.state,__ATOMIC_ACQUIRE);
 if(!shelf_scene_state.grain.data)return;
 if(state==2 && grain_job.buf) {
  memcpy(shelf_scene_state.grain.data,grain_job.buf,shelf_scene_state.grain.size);
  svcStoreProcessDataCache(CUR_PROCESS_HANDLE,(u32)shelf_scene_state.grain.data,shelf_scene_state.grain.size);
  free(grain_job.buf);grain_job.buf=NULL;return;
 }
 if(state || !grain_job.buf)return;
 __atomic_store_n(&grain_job.state,1,__ATOMIC_RELEASE);
 if(!bb_index_defer_low(grain_work,NULL)) {
  /* No index worker (missing root / thread allocation failure): one CPU-only
   * row per frame. Never perform cache I/O on this fallback. */
  if(!bb_index_cancelled() && grain_job.part<G_COUNT) {
   make_grain_part(grain_job.buf,grain_job.part,grain_job.row,grain_job.row+1,&grain_job.wood);
   grain_job.row++;
   if(grain_job.row>=shelf_scene_grain_region[grain_job.part][3]){grain_job.part++;grain_job.row=0;}
   grain_job.ready=grain_job.part==G_COUNT;
  }
  __atomic_store_n(&grain_job.state,grain_job.ready?2:0,__ATOMIC_RELEASE);
 }
}
void shelf_scene_grain_start(C3D_Tex *t)
{
 memset(t->data,128,t->size);
 if(!grain_job.buf) {
  grain_job.buf=malloc(t->size);if(grain_job.buf)memset(grain_job.buf,128,t->size);grain_job.size=t->size;grain_job.part=grain_job.row=0;grain_job.ready=false;
  __atomic_store_n(&grain_job.state,0,__ATOMIC_RELEASE);
 }
 shelf_scene_grain_step();
}

// White radial light (LA8) and the matching darkness away from a light (A8).
bool shelf_scene_radial(C3D_Tex *t, bool dark)
{
	if (!C3D_TexInit(t, 64, 64, dark ? GPU_A8 : GPU_LA8)) return false;
	u8 *d = t->data;
	for (int y = 0; y < 64; y++)
		for (int x = 0; x < 64; x++) {
			float r = sqrtf((x - 31.5f) * (x - 31.5f) + (y - 31.5f) * (y - 31.5f)) / 32;
			if (dark) d[texel(x, y, 64)] = (u8)(255 * clampf(r < .06f ? 0 : r < .55f ? (r - .06f) / .49f * .35f : .35f + (r - .55f) / .45f * .65f, 0, 1));
			else {
				float a = r >= 1 ? 0 : (1 - r) * (1 - r);
				u32 i = texel(x, y, 64);
				d[2 * i] = (u8)(255 * a);   // alpha
				d[2 * i + 1] = 255;         // luminance
			}
		}
	C3D_TexSetFilter(t, GPU_LINEAR, GPU_LINEAR);
	C3D_TexSetWrap(t, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	svcStoreProcessDataCache(CUR_PROCESS_HANDLE, (u32)t->data, t->size);
	return true;
}
