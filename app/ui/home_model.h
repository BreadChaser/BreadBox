// Places shared by the room and list presentations, in navigation order.
#pragma once
#include <stdbool.h>
#include <stddef.h>

enum { E_CONTINUE, E_LIBRARY, E_HOST, E_JOIN, E_SHARE, E_SETTINGS, E_QUIT, E_TOGETHER };
typedef struct {
 int action, place;
 const char *name, *card_title;
 unsigned char icon;
 bool needs_continue;
} bb_home_place;

int bb_home_count(bool have_continue);
const bb_home_place *bb_home_at(int row, bool have_continue);
const bb_home_place *bb_home_find(int action);
const bb_home_place *bb_home_place_at(int place);
int bb_home_neighbor(int place, int step, bool have_continue);
int bb_home_room_places(int *places, int capacity, bool have_continue);
