#pragma once
#include <stdbool.h>

/* Optional, borrowed scene. Controller calls enter/leave once and tick once per
 * frame (never per eye); settled means the requested camera move has completed.
 * draw_top draws into the bound top target and restores its overlay state.
 * No ownership transfer, room types, service policy, or allocations. */
typedef struct ui_destination {
 void *userdata;
 void (*enter)(void *userdata);
 void (*leave)(void *userdata);
 void (*tick)(void *userdata, float dt);
 bool (*settled)(void *userdata);
 void (*draw_top)(void *userdata);
} ui_destination;

/* Scoped dialog backdrop; restore the returned pointer before its lifetime ends.
 * NULL restores the usual menu backdrop. Gameplay dialogs retain their game. */
const ui_destination *ui_dialog_scene_set(const ui_destination *scene);

/* Compose a scene and a shared 2D overlay without exposing scene GPU state. */
void ui_destination_draw_top(const ui_destination *scene, void (*overlay)(void *), void *userdata);

