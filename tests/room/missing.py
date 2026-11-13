#!/usr/bin/env python3
"""Without its room there is no main menu (review m5): home_run's first act after a failed room load is the one
error card, through the shared dialog (ui_confirm_action), then BreadBox closes (HOME_QUIT). After a game (the
Library, review N1) shelf_run leaves first without its room, so home_run shows the same card. No tile home, no
standalone shelf room is left to fall back to. Compiles the production room_missing() with fakes."""
import pathlib, re, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[2]
home = (root / 'app/ui/home.c').read_text()
run = home[home.index('int home_run('):]
start = home[home.index('static bool room_start(void)'):home.index('static int room_missing(void)')]
assert re.search(r'bool ok = room_scene_open\(\);\s*boot_mark\("room file"\);\s*if \(!ok\)\s*return false;', start), 'room_start must stop at a failed load'
assert re.search(r'if \(!room_start\(\)\)\s*return room_missing\(\);', run), 'home_run must show the card when the room did not load'
assert 'room_on' not in home and 'shelf_theme_active' not in home, 'a room-less main menu is back'
# review N1: after a game main.c opens the Library (bb_platform_restore_menu ignores a failed room load); without its room
# the Library leaves before anything else (4), main goes home (at_home), and home_run shows the card. tests/shelf runs it.
shelf = (root / 'app/ui/shelf.c').read_text()
body = shelf[shelf.index('int shelf_run(char *path, size_t cap)\n{'):]
first = [l.strip() for l in body.split('\n')[2:] if l.strip() and not l.strip().startswith('//')][0]
assert first == 'if (!shelf_scene_in_room()) return 4;', 'shelf_run must leave first when the room is missing: ' + first
scene = (root / 'app/ui/shelf_scene.c').read_text()
assert re.search(r'bool shelf_scene_in_room\(void\) \{ return in_room\(\); \}', scene)
assert re.search(r'static bool in_room\(void\) \{ const room_file \*f = room_scene_file\(\); return f && ', scene)
main = (root / 'app/ui/main.c').read_text()
assert re.search(r'int r = breadbox_systems_run\(path, sizeof\(path\)\);[\s\S]{0,300}if \(r == 4\) \{ at_home = true; continue; \}', main), 'main: 4 goes home'
assert 'int breadbox_systems_run(char *path, size_t len) { return shelf_run(path, len); }' in (root / 'app/ui/breadbox_systems.c').read_text()
for gone in ('app/ui/breadbox_favorites.c', 'app/ui/components/breadbox_systems.inc'):
    assert not (root / gone).exists(), gone
text = re.search(r'#define HOME_ROOM_MISSING_TEXT ("[^"]*")', home)
missing = home[home.index('static int room_missing(void)'):home.index('int home_run(')]
code = '''#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
enum { HOME_QUIT, HOME_AGAIN };
#define HOME_ROOM_MISSING_TEXT %s
static int cards, leaves;
static char title[128], message[256], action[64];
static void log_printf(const char *f, ...) { (void)f; }
static bool ui_confirm_action(const char *t, const char *m, const char *a, bool game)
{ cards++; snprintf(title, sizeof title, "%%s", t); snprintf(message, sizeof message, "%%s", m); snprintf(action, sizeof action, "%%s", a); assert(!game); return true; }
static void leave(void) { leaves++; }
%s
int main(void)
{
	assert(room_missing() == HOME_QUIT && cards == 1 && leaves == 1);
	assert(strstr(title, "room") && strlen(message) < 160 && strstr(action, "Close"));
	static const char *british[] = { "colour", "centre", "behaviour", "favourite", "grey", "cancelled", NULL };
	for (int i = 0; british[i]; i++) assert(!strstr(message, british[i]) && !strstr(title, british[i]));
	puts("room missing: one card through the shared dialog, then BreadBox closes: PASS");
}
''' % (text.group(1), missing)
with tempfile.TemporaryDirectory() as td:
    p = pathlib.Path(td)
    (p / 'test.c').write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                    str(p / 'test.c'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('room missing after a game: the Library leaves first (4), main goes home, the card follows: PASS')
