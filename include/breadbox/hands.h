// The hands (BBH1, tools/room/source/SPEC.md): one rigged hand mesh, its style textures and
// baked clips (take out, hold, turn, put back, game start, picker), made by the UI
// mock. hands.c is portable (file, sampling, forward kinematics, smooth normals,
// which clip the shelf is in); the GPU side is in room_scene.c. Everything is read
// from the file's own tables, so a re-exported hands.bbh needs no code change.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HANDS_MAX_BONES 17       // the shader's matrix palette
#define HANDS_OBJECTS 5          // case lid flap cart card
enum { HANDS_CASE, HANDS_LID, HANDS_FLAP, HANDS_CART, HANDS_CARD };
enum { HANDS_SPACE_SLOT, HANDS_SPACE_CARD, HANDS_SPACE_CAMERA };
// A clip's box (BBH1 v3 clip `box` byte; names end in the class: pick_ps1, hold_gba, turn_r1_ps1p, open_ps1d):
// none, a jewel case (ps1, 10.5 mm), a cardboard box (gba), a PAL case (ps1p, 17 mm), a multi-disc case (ps1d, 25 mm)
enum { HANDS_BOX_NONE, HANDS_BOX_CASE, HANDS_BOX_CARTON, HANDS_BOX_CASE_PAL, HANDS_BOX_CASE_MULTI };
static inline bool hands_box_is_case(int box) { return box == HANDS_BOX_CASE || box == HANDS_BOX_CASE_PAL || box == HANDS_BOX_CASE_MULTI; }
// The clip class of a held box from its thickness (mm): cases up to 12 ps1, up to 20 ps1p, thicker ps1d
static inline int hands_box_class(bool carton, float thickness)
{
	return carton ? HANDS_BOX_CARTON : thickness <= 12 ? HANDS_BOX_CASE : thickness <= 20 ? HANDS_BOX_CASE_PAL : HANDS_BOX_CASE_MULTI;
}

typedef struct {
	char name[16];
	uint8_t space, box, loop, flags;
	uint16_t frames, frame_bytes;
	uint32_t mask, offset;
	float thw[3];
	uint32_t pad;
} hands_clip;

typedef struct {
	const uint8_t *data;
	size_t size;
	unsigned version, fps, ntex, npart, nbone, nclip, nvert, nidx;
	const uint8_t *vert;          // 16 bytes each: s16 x y z bone, s16 u v, u8 r g b a
	const uint16_t *idx;
	struct { char style[9]; unsigned w, h; const uint16_t *px; } tex[8];
	struct { char name[13]; unsigned first, count; } part[8];
	struct { float head[3], flex[3], spread[3]; int parent; } bone[HANDS_MAX_BONES];
	const hands_clip *clip;
	float glove[6][2];            // uv offset (texels) of each glove tile: bare, knit, leather, fingerless, work, mittens
	float (*rest)[2][2];          // hands_prepare: per keyframe and hand, the index's rest (spread, curl)
	uint32_t *rest_first;         // the first keyframe of each clip in `rest`
	uint8_t *rest_done;           // all of the clip's keyframes have their rest (hands_prepare_step)
	uint8_t *rest_key;            // this keyframe has its rest (ahead, or the first time it is shown)
} hands_file;

bool hands_parse(hands_file *f, const void *data, size_t size, char *err, size_t cap);
const hands_clip *hands_clip_named(const hands_file *f, const char *name, bool mitten);  // "<name>_m" for mittens when there is one
// "<action>_..." for this box (the clip's own box field), the mitten twin when asked and present;
// a PAL or multi-disc case without its own clip falls back to the jewel case's (class 1)
const hands_clip *hands_clip_for(const hands_file *f, const char *action, int box, bool mitten);
float hands_clip_seconds(const hands_file *f, const hands_clip *c);
float hands_loop_seconds(const hands_file *f, const hands_clip *c);   // a loop clip's period ((frames - 1) / fps)
bool hands_part(const hands_file *f, const char *name, unsigned *first, unsigned *count);
// The hands' sleeve texture is there (texture 0, a size): the room's hands need it. Compact camera/case tracks
// (case-motion.bbh) have no textures and parse fine; drawing hands from such a file would read past its tables.
bool hands_has_sleeve(const hands_file *f);

// A frame: matrices are 3x4 row-major (rotation, translation in mm), in the clip's space.
typedef struct {
	bool shown[2];               // right, left hand
	float fade;                  // the game-start fade to black, 0..1
	float cam[7];                // camera position, look-at, focal length (px at 400 wide)
	float palm[2][12], sleeve[2][12], joint[2][2 * HANDS_MAX_BONES];
	uint32_t objects;            // bit i: obj[i] is set
	float obj[HANDS_OBJECTS][12];
} hands_frame;
// Interpolated at t (seconds), every display frame: rotations blended as quaternions,
// positions, joint angles and the camera on Catmull-Rom curves through the keys.
void hands_sample(const hands_file *f, const hands_clip *c, float t, hands_frame *out);
void hands_frame_mix(hands_frame *a, const hands_frame *b, float k);   // a towards b by k (0..1)
// A clip that changes (take out -> hold, hold -> turn, an interruption) does not snap: for
// HANDS_BLEND seconds the frame blends from where the last clip was (still running, a loop
// looping) to the new one. Feed it the clip and time chosen for this frame (twice with the
// same values does nothing); `now` is the caller's clock in seconds.
#define HANDS_BLEND .2f
typedef struct { const hands_clip *c, *from; float t, from_t, since; } hands_track;
void hands_track_set(hands_track *k, const hands_clip *c, float t, float now);
void hands_track_sample(const hands_file *f, const hands_track *k, float now, hands_frame *out);
// Bone matrices (3x4 row-major, clip space) of one hand of a frame.
void hands_pose(const hands_file *f, const hands_frame *fr, int hand, bool mitten, float out[][12]);
// A hand holding the case whose index fingertip stands up over the case's top
// (UI mock round 3e's open/hold grips): turn and curl the index until the tip
// rests on the case, no point of it inside. hands_sample() applies it to every
// keyframe; no change when the tip already rests (a fixed export).
void hands_rest_index(const hands_file *f, const hands_clip *c, hands_frame *fr);
bool hands_prepare(hands_file *f);   // room for every keyframe's index rest (each clip filled when it first plays)
extern bool hands_prepare_mitten;   // hands_prepare_step does the clips of this glove first
bool hands_prepare_step(hands_file *f);   // fill the next clip's rests ahead of time; true when all are done
void hands_free(hands_file *f);
// Smooth normals of the mesh at rest: face normals summed over every vertex at the
// same position (the mesh is split at uv seams), unit length x 127.
void hands_normals(const hands_file *f, int8_t (*out)[4]);

void hands_mul34(const float a[12], const float b[12], float out[12]);   // a . b (affine)

// The bookshelf's hand: which clip and at what time, from the shelf's own state.
// side: 0 front, 1 spine, 2 back (L/R turn the held box); `turn_from` the side before.
typedef struct {
	float t;                     // shelf time (s)
	float pull, pull_to, pull_t0, pull_seconds;   // the take-out (0 in the shelf, 1 in the hand)
	float play_t0;               // Play pressed (< 0: not)
	int side, turn_from;         // 0 front, 1 spine, 2 back, 3 the open edge (shelf_input.h: L / R round and round)
	int turn_steps;              // the turn from turn_from to side in quarters: + R, - L
	float turn_t0;
	int rev;                     // 1: the take-out backwards, 2: the put-back backwards, from rev_t seconds
	float rev_t;
} hands_shelf_state;
// The case's turn about its vertical axis, over the clip's (room_hands_draw): none; add yaw (the open edge: the case
// half round in the spine's grip); or set the case's yaw to yaw (a turn under way: always the way pressed).
enum { HANDS_YAW_NONE, HANDS_YAW_ADD, HANDS_YAW_SET };
#define HANDS_GRIP(side) ((side) == 3 ? 1 : (side))   // the hand's grip at a side: the open edge is held as the spine
typedef struct { const hands_clip *clip; float t; float camera_k; int yaw_mode; float yaw; } hands_shelf_clip;
float hands_case_yaw(const hands_frame *fr);   // radians, atan2 of the case's z axis (x over z)
void hands_case_turn(hands_frame *fr, float delta);   // the case and its parts turned about its vertical: yaw + delta
hands_shelf_clip hands_for_shelf(const hands_file *f, int box, bool mitten, const hands_shelf_state *s);
