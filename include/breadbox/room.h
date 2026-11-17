// BreadBox room main menu: the home screen as a small PS1-style 3D room
// (UI mock R10, js/breadbox-r10-room3d.js). Every place of the main menu is an
// object in the room (Continue = the TV, Library = the bookcase, Settings = the
// scope, Play together = the cushions, Share = the coffee table); the camera
// glides between them. The models come from one BBR v3 file (the Lamplight room,
// tools/room/source/SPEC.md) with one baked colour stream per room style, drawn
// with citro3d in a handful of draws.
//
// room_bbr.c is portable (file tables, poses, camera, navigation, the
// bookcase's far view); room_scene.c owns the GPU side; home.c drives it.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---- BBR file (little endian, every table 4-byte aligned) ------------------
// Version 3: the room (per-vertex light, a colour stream per room style). Versions 1-2
// (stride 16 or 20): props.bbr, read as plain meshes.
typedef struct {
	char magic[4];
	uint16_t version, flags, ntex, npart, nanchor, stride;
	uint32_t nvert, nidx;
	char style[8];
} room_bbr_header;
typedef struct {
	char name[8];
	uint16_t w, h, fmt, pad;
	uint32_t offset, size;
} room_bbr_tex;
typedef struct {
	char name[12];
	uint8_t mode, tex, flags, pad;
	char group[12];
	uint32_t first, count;
	float pivot[3];
} room_bbr_part;
typedef struct {
	char name[16];
	float v[10];
} room_bbr_anchor;
typedef struct {
	int16_t x, y, z, w;        // position (mm); w = palette slot, or a sprite's size
	int16_t u, v;              // texels x 16
	uint8_t r, g, b, a;        // baked light x tint (128 = 1.0, up to 2x); a = alpha, or a sprite's corner
} room_vertex;                 // the GPU's format (16 bytes)
typedef struct {
	char id[8];
	uint32_t colours, probes;  // file offsets: nvert x rgba, nprobe x room_probe
	uint16_t nprobe, atlas;    // atlas: its texture's index
	uint16_t range, pad;       // light range: 0 or 2 (128 = 1.0, the PS1 x2), 4 (255 / 4 = 1.0)
} room_bbr_style;
typedef struct {
	float pos[3];
	uint8_t cube[6][3];        // irradiance seen along +x -x +y -y +z -z, light / 2
	uint8_t pad[2];
} room_probe;
enum { ROOM_TEX_RUNTIME, ROOM_TEX_RGBA5551, ROOM_TEX_RGB565, ROOM_TEX_RGBA4444 };
enum { ROOM_MODE_OPAQUE, ROOM_MODE_BLEND, ROOM_MODE_ADD };

typedef struct {
	const room_bbr_header *head;
	const room_bbr_tex *tex;
	const room_bbr_part *part;
	const room_bbr_anchor *anchor;
	const room_bbr_style *style;
	unsigned nstyle, cur;      // v3: room styles, the one in use (room_style_use)
	const uint8_t *vdata;      // the file's vertices (stride 16, or 20 in v2 props): room_vertex_at()
	unsigned stride, nprobe;
	float lrange;              // the style in use's light range (2 or 4)
	const room_probe *probe;   // the style in use's probes
	const uint16_t *idx;
	size_t size;
	unsigned gen;              // the light generation: new on every room_style_use (room_parse too: style 0); a mesh lit from
	                           // the file (the real boxes) is stale when it differs (room_mesh_key)
} room_file;

bool room_parse(room_file *f, const void *data, size_t size, char *err, size_t cap);
room_vertex room_vertex_at(const room_file *f, unsigned i);
// Room styles (v3): the index of a style id (-1: none), and choosing one: its probes from now
// on; its colours (nvert x rgba) are room_style_colours (NULL once the vertices are dropped after load). The parsed
// file starts on style 0.
int room_style_find(const room_file *f, const char *id);
void room_dither_set(float k);   // the final pass's dither strength (0..1), before the pattern is made
float room_light_unit(const room_file *f);   // vertex colour bytes per 1.0 of light in its style in use (128, or 255 / 4 for range 4)
bool room_style_use(room_file *f, int i);
const uint8_t *room_style_colours(const room_file *f, int i);
// Light at a point seen from a direction (toward the camera), from the 4 nearest lit
// probes of the style in use: 1.0 = neutral; false without probes (out stays 1).
bool room_probe_light(const room_file *f, const float pos[3], const float dir[3], float out[3]);
// the same as vertex colour bytes (room_light_unit = 1.0) times k, for meshes made at run time
void room_probe_rgb(const room_file *f, const float pos[3], const float dir[3], float k, uint8_t rgb[3]);
const room_bbr_part *room_part(const room_file *f, const char *name);
const float *room_anchor(const room_file *f, const char *name);
const room_bbr_tex *room_tex(const room_file *f, const char *name);

// ---- places and navigation --------------------------------------------------
enum { ROOM_CONTINUE, ROOM_LIBRARY, ROOM_SETTINGS, ROOM_TOGETHER, ROOM_SHARE, ROOM_PLACES };
enum { ROOM_POSTER = ROOM_PLACES, ROOM_BOARD };   // the walls (room_walls.h): no tile, the camera only looks
enum { ROOM_UP, ROOM_DOWN, ROOM_LEFT, ROOM_RIGHT };
extern const char *const room_place_id[ROOM_PLACES];   // "continue", ... (anchor names)
extern const char *const room_place_group[ROOM_PLACES]; // "p_tv", ... (palette slots 1-5)
int room_nav(int place, int dir);      // the place in that direction, -1 none
int room_cycle(int place, int step);   // L/R: previous/next place

// ---- camera -----------------------------------------------------------------
typedef struct {
	float pos[3], look[3];
	float F;        // focal length, px at 400 wide
	float eye;      // eye separation (mm)
	float conv;     // zero-parallax distance (mm)
} room_pose;
bool room_pose_of(const room_file *f, const char *id, room_pose *out);
room_pose room_mix(const room_pose *a, const room_pose *b, float u);
// Clip-space matrix for the 3DS top screen (rows; tilted, PICA depth -w..0).
// eye: this eye's offset in mm (0 mono), as the mock's camera(): the image is
// shifted so the conv plane has no parallax.
void room_camera(const room_pose *p, float eye, float out[16], float right[3], float up[3], float at[3]);
// the same with the look-at point on screen row cy (the bookshelf's camera)
void room_camera_ex(const room_pose *p, float eye, float cy, float out[16], float right[3], float up[3], float at[3]);

// ---- the main menu's motion -------------------------------------------------
enum { ROOM_HOME, ROOM_GO, ROOM_AT, ROOM_BACK };
#include "ui_style.h"
#define ROOM_GLIDE UI_MOTION_CAMERA_S
extern bool room_show_hands;   // roomhands=on: optional first-person hands (default hidden)
typedef struct {
	const room_file *file;
	float t, t0;
	int mode, focus, place;
	room_pose from;
	float att[6], attv[6];   // home camera spring: pos, look
	room_pose lib;           // the Library's pose when set (the bookcase's wall view), else its anchor
	bool lib_on;
	bool nofocus;            // Quit is the bar's focus: no place is picked out, the camera stays at the plain home view
	float invitation;        // Share card lift, 0..1; no continuous motion
	float turn;              // 0..1: a small glance away (the Quit; room_view_turn)
} room_view;
void room_view_init(room_view *v, const room_file *f, int focus, int at_place);
void room_view_step(room_view *v, float dt);
void room_view_go(room_view *v, int place);
void room_view_back(room_view *v);
void room_view_turn(room_view *v, float k);   // at home: the camera turned k (0..1) of the exit glance, on the glides' easing
room_pose room_view_pose(const room_view *v);
float room_view_tint(const room_view *v, int place);   // focus glow on a place's slot (1 = none)
bool room_view_arrived(const room_view *v);   // ROOM_AT (a glide in has ended)
// The Library's pose from now on (the wall view of the bookcase, which follows the
// highlighted section); a glide back from the Library that has not started moving
// starts from it too.
void room_view_set_library(room_view *v, const room_pose *p);

// ---- the bookcase's far view: one quad per section + its brass plate ------
// Sections fill the case from the top left: Favorites first, then systems by
// release, newest first (the caller orders them). Vertices go to out (6 per
// quad as two triangles, no index buffer); returns the vertex count.
enum { ROOM_SPINE_MIXED, ROOM_SPINE_JEWEL, ROOM_SPINE_CARTON, ROOM_SPINE_STYLES };
typedef struct {
	int style;          // ROOM_SPINE_*
	int games;
	float thickness;    // mm per box (spine)
} room_section;
// The far-view texture: 128 x 64 tiles (a cubby's inside, 356 x 168 mm): spines per
// style, the cubby's wood back, a "star a game" card, decor; and the brass plate.
#define ROOM_SHELF_W 256
#define ROOM_SHELF_H 512
// Which cubby each section fills: one cubby of the
// 24 per system, never shared, never more than one, from the top left: Favorites, then the
// systems newest first; a system with more boxes than its cubby holds slides them behind its
// uprights, one with fewer gets a bookend after its boxes. Cubbies nobody uses get decor.
#define ROOM_CUBBIES 24
enum { ROOM_DECOR_BOOKS, ROOM_DECOR_PLANT, ROOM_DECOR_PHOTO, ROOM_DECOR_TROPHY, ROOM_DECOR_CASSETTES, ROOM_DECORS };
typedef struct {
	int nsec, used, cubbies;                         // sections placed, cubbies they use, cubbies in the case
	int first[ROOM_CUBBIES], count[ROOM_CUBBIES];    // section -> its cubbies
	signed char owner[ROOM_CUBBIES];                 // cubby -> section, -1 decor
	signed char decor[ROOM_CUBBIES];                 // cubby -> ROOM_DECOR_*, -1 none
	float fill[ROOM_CUBBIES];                        // cubby -> how full of boxes, 0..1
} room_case_plan;
// The far view by a plan: every cubby's wood back first (vertices 0 .. *backs), then per
// cubby c its boxes / decor / card and, on a section's first cubby, its plate: vertices
// first[c] .. first[c + 1] (first: ROOM_CUBBIES + 1 entries, may be NULL). Every cubby is
// lit by the room style's probes in front of it (room_cubby_light).
int room_case_mesh(const room_file *f, const room_section *s, const room_case_plan *p, int slot,
	room_vertex *out, int cap, int *first, int *backs);
// One section of the case (i from the top left, as room_case_mesh), mm.
typedef struct {
	int row, bay;            // case row (0 = bottom) and bay
	float xl, xr, xin0, inner, cx;   // section edges, inside the uprights, its middle
	float base, top;         // the board it stands on, the underside of the board above
} room_case_geo;
bool room_case_section(const room_file *f, int i, room_case_geo *g);
// the light in front of a cubby (the room style's probes), vertex colour bytes x k
void room_cubby_light(const room_file *f, const room_case_geo *g, float k, uint8_t rgb[3]);
// the same light as 1.0 = neutral (the real boxes on that cubby's shelf)
void room_cubby_light_f(const room_file *f, const room_case_geo *g, float light[3]);
// The bookcase lamp brightens the selected system:
// when its cubby opens, the strip under the board over it switches on (lamp 0..1,
// eased up as the camera arrives, ~0.2 s). Vertex light only: the open cubby's back becomes a small lit
// grid, the real boxes are lit per vertex, the case wood takes a small tint; no per-pixel pass.
// the lamp's light added at a point (mm) of cubby g, 1.0 = neutral (0 when lamp is 0)
void room_lamp_at(const room_case_geo *g, float lamp, float x, float y, float z, float out[3]);
// the case wood's tint while the lamp is on (1 = none)
void room_lamp_case_tint(float lamp, float out[3]);
// The open cubby's back as a lit grid (replaces its plain back) and, lamp > 0, the lamp's glowing
// face: vertices to out (as room_case_mesh), returns the count.
int room_lamp_mesh(const room_file *f, int cubby, float lamp, int slot, room_vertex *out, int cap);
// The light a game box gets: the light's brightness without its colour, so cartons keep their printed
// colours and black plastic stays black under a warm room or lamp; above KNEE it
// rises a quarter as fast, so a lit carton's colours never wash out to white.
#define ROOM_BOX_KNEE .9f
void room_box_light(const float in[3], float out[3]);
// The black jewel case's plastic in the spine atlas (an RGB565 render target): exact greys only,
// multiples of 8, which keep red, green and blue at one level. A grey between them loses more of its
// red and blue than its green and reads green once the shelf's light multiplies it.
// Bands top down: the middle, the rounded sides; the rails
// along both long edges; the top and foot edges.
#define ROOM_JEWEL_MID { 32, 24, 24, 16 }
#define ROOM_JEWEL_SIDE { 24, 16, 16, 8 }
#define ROOM_JEWEL_RAIL 56
#define ROOM_JEWEL_EDGE 40
// a runtime mesh's vertex colour: colour c (0xAABBGGRR) x k x light (1.0 = neutral) in the style's unit
void room_lit_rgb(const room_file *f, uint32_t c, float k, const float light[3], uint8_t out[3]);
void room_case_plan_make(const room_file *f, const room_section *s, int n, room_case_plan *p);
// a cubby its system does not fill: the cubby and x (mm) of the bookend at the right end of its boxes
bool room_case_bookend(const room_file *f, const room_case_plan *p, int sec, int *cubby, float *x);
int room_case_sections(const room_file *f);   // rows x bays (0: no case)
// The library's cameras (UI mock R10): the wall view, four rows of the case at a time
// following hy (the highlighted section's base, sprung); `owned` sections are filled.
room_pose room_wall_pose(const room_file *f, int owned, float hy);
// Close to one section, head on: camx along the case (mm), hmax the tallest box,
// pk 0..1 a box in the hand (the camera steps back), speed 0..1 scrolling fast.
room_pose room_section_pose(const room_file *f, float camx, float base, float hmax, float pk, float speed);
// The far-view texture, RGBA5551, rows top first (as the BBR1 atlas).
void room_shelf_texture(uint16_t *px);

// ---- the real boxes' mesh cache (room_scene.c) -------------------------------
// Made again when its section, its boxes or the generation change. The generation changes
// with every room load and every room style (their light), so a cache never outlives either.
typedef struct { unsigned gen; int cur, n; bool built; } room_mesh_key;
bool room_mesh_key_stale(const room_mesh_key *k, unsigned gen, int cur, int n, bool same_boxes);
void room_mesh_key_set(room_mesh_key *k, unsigned gen, int cur, int n);

// ---- the final pass (room_scene.c): the PS1's 4x4 ordered dither down to RGB565 ----------
// One quad adds the PS1's dither matrix (+4: 0..7 for red and blue, half that for green) to the
// frame's 8-bit channels, a second ANDs them with ROOM_DITHER_MASK (RGB565 precision): the
// room ends dithered to 16 bits like a PS1 frame, at the cost of two full-screen fills.
#define ROOM_DITHER_MASK 0xFFF8FCF8u                  // the AND quad's constant (0xAABBGGRR)
void room_dither_texture(uint32_t px[64]);            // 8x8 RGBA8 (0xRRGGBBAA), rows top first: the 4x4 pattern twice each way
uint8_t room_dither_channel(int x, int y, int c, uint8_t v);   // one pixel's channel c (0 r, 1 g, 2 b) through the pass

// room_pace.c: frame pacing per moment of the room and the Library (the log's "pace:" lines)
enum { ROOM_PACE_BEFORE, ROOM_PACE_TOP, ROOM_PACE_BOTTOM, ROOM_PACE_SPINES, ROOM_PACE_HANDS, ROOM_PACE_LIBRARY, ROOM_PACE_TV,
	ROOM_PACE_PRELOAD, ROOM_PACE_LABELS, ROOM_PACE_LOGIC, ROOM_PACE_COVER, ROOM_PACE_PARTS };
void room_pace_frame(const char *label);   // once per frame before presenting it; label: a string constant
void room_pace_add(int part, uint64_t ticks);   // time this frame spent on a part
void room_pace_hook(void);                 // time the frame-start work (after covers_pump / shelf_scene_frame)
void room_pace_restart(void);              // a screen starts: the time before it is not a frame
bool room_pace_priority(bool on);          // Old 3DS: the menu thread above the covers' workers while the room runs; was it?
bool room_hands_preload_step(void);   // the hands' file and their rests, a part per call; true when ready
void room_hands_prepare_ms(float ms);  // how long a call may work on the rests (1.5 ms; more while nothing moves)

// ---- room_scene.c (3DS): the GPU side ---------------------------------------
#ifdef __3DS__
#include <citro3d.h>
#include <tex3ds.h>
#include "hands.h"
bool room_scene_open(void);              // romfs room file + textures, once; false: no room
const room_file *room_scene_file(void);
const room_case_plan *room_scene_case_plan(void);   // which cubbies the Library's sections fill (NULL: none yet)
void room_scene_library(void);           // the bookcase's far view from the registry
void room_scene_library_follow(bool now); // every frame: made again when the library has changed and settled
// The TV's picture, baked once with the CRT look (curve, scanlines, glow) when
// it changes: the last game's picture (RGB565 256x192 rows top first), a tiled
// RGB565 texture region (a save thumbnail, a cover), or NULL for a static channel.
void room_scene_tv(const u16 *px);
void room_scene_tv_tex(C3D_Tex *t, const Tex3DS_SubTexture *sub);
bool room_scene_picture_tex(C3D_Tex *t, const u16 *px);   // the TV's picture for the home card
// Hands in the room (the Settings picker): a clip in the camera's space at t.
// One eye of the room into the bound top target (ui_eye, ui_parallax), before
// any 2D drawing of the frame; hands_clip (camera space) may be NULL.
void room_scene_draw(const room_view *v, const hands_clip *hands, float hands_t);
// The Library inside the room's bookcase (shelf_scene.c): far tiles per section, the
// highlighted section brighter on the wall view, and the real boxes of one section
// (their baked spines, lit by the room style's light probes) cross-fading in over its tile.
typedef struct {
	float x, base, z;          // the spine: centre x, the board it stands on, its plane (mm)
	float t, h, w;             // thickness, height, depth into the shelf (mm)
	C3D_Tex *spine;            // a baked spine, NULL: plain colour
	float u0, v0, u1, v1;      // its part of the texture (citro3d uv)
	u32 color, side;           // plain spine colour, top and sides (C2D colours)
	float bright;              // 1; more for the chosen box
} room_box;
typedef struct {
	int hsel;                  // highlighted section (-1): its cubbies lit
	float wall;                // the wall view's emphasis 0..1
	int cur;                   // the cubby with real boxes (-1)
	float fade;                // its boxes over its tile 0..1
	float lamp;                // the lamp over that cubby 0..1 (room_lamp_at)
	const room_box *box;
	int nbox;
	// after the room and the boxes, in the room's camera and depth (the hands)
	void (*after)(const float proj[16], void *ud);
	void *ud;
} room_library;
void room_scene_draw_library(const room_pose *p, float t, const room_library *lib);
// the room's light on the hands at pos, seen along to_cam (its probes; room_hands_light)
void room_scene_hands_light(const float pos[3], const float to_cam[3]);
void room_scene_overlay_begin(void);     // 2D on top of the room: no depth test
void room_scene_overlay_end(void);
void room_scene_release(void);           // before a game: frees everything
// The room style to draw (an id of the file's style table: "day", "night"; unknown or NULL:
// the file's first). Cheap to call every frame: a change only picks the style's colour buffer, atlas and probes
// (all made at load) and relights the case and the walls.
void room_scene_style(const char *id);

// room_hands.c: the hands and the case they hold (hands.h clips)
typedef struct {
	int cover_id;                      // the case texture is remade when it changes
	C3D_Tex *cover;                    // front art (RGB565, NULL: plain)
	const Tex3DS_SubTexture *cover_sub;
	u32 color;                         // box colour (r | g << 8 | b << 16)
	float thw[3];                      // the real box: thickness, height, width (mm)
	C3D_Tex *spine;                    // its baked spine (RGB565, the bookshelf's atlas), NULL: plain
	Tex3DS_SubTexture spine_sub;
	C3D_Tex *back;                     // its back (the shelf's generated one), NULL: the cover, darker
	Tex3DS_SubTexture back_sub;
	u32 side;                          // the case's outer sides (top, bottom, open edge): the shelf's side colour
	int yaw_mode;                      // hands.h HANDS_YAW_*: the case's turn over the clip's (L / R round and round)
	float yaw;
} room_hands_case;
bool room_hands_open(void);
void room_hands_release(void);
const hands_file *room_hands_file(void);
// Draw a clip's frame at t: its case (box != NULL) and hands, `place` = clip space -> world (3x4),
// `proj` = world -> clip rows (room_camera). The caller binds the target. Returns the frame's fade.
float room_hands_draw(const hands_clip *c, float t, const float place[12], const float proj[16], const room_hands_case *box);
// the case of the box about to be taken out, a few bands per call (budget in ticks): true when made
bool room_hands_case_warm(const room_hands_case *box, u64 budget);
bool room_hands_camera(const hands_clip *c, float t, float cam[7]);
// The track (hands.h hands_track: clip changes blended) the draws and the camera sample
// when they show its clip; set once per frame by whoever plays the hands.
void room_hands_use_track(const hands_track *k, float now);
void room_hands_light(const float rgb[3]);   // the room's light on the hands (probes); NULL: the usual warm light
#endif
// an anchor's point on the last drawn frame's top screen (400x240, mono) and its width (v[3] mm) there in px
#ifdef __3DS__
bool room_scene_anchor_screen(const char *name, float *x, float *y, float *w);
#else   // host checks have no room: the fixed placement
static inline bool room_scene_anchor_screen(const char *name, float *x, float *y, float *w) { (void)name; (void)x; (void)y; (void)w; return false; }
#endif

#if defined(__3DS__)
bool room_scene_tv_bake(const uint16_t *pixels, uint16_t *tiled, uint16_t *card);
void room_scene_tv_upload(const uint16_t *tiled, bool snow);
bool room_scene_picture_upload(C3D_Tex *tex, const uint16_t *tiled);
void room_scene_tv_poll(void);
#endif

void room_pace_presented(void);
void room_pace_diagnostic(void);
void room_pace_return_start(void);
bool room_pace_return_pending(void);

// After index shutdown; false means a cancelled callback still owns map readers.
bool home_release_cache(void);
void room_scene_tv_maps_release(void);
