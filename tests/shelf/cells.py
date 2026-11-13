#!/usr/bin/env python3
"""Exercise production spine-cell sizing, including the empty Favorites row."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
src = (root / 'app/ui/shelf_scene.c').read_text()
a = src.index('static int px_of(')
b = src.index('static int spine_width(', a)
with tempfile.TemporaryDirectory(prefix='shelf-cells-') as td:
    p = Path(td) / 'cells.c'
    p.write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef unsigned u32;
#define K 2
#define ATLAS 512
#define MAX_CELLS 64
#define ROOM_BOXES 31
typedef struct { float t; } shelf_box;
typedef struct { int n; shelf_box *box; float hmax, s; } shelf_row;
static struct { unsigned layout; shelf_row rows[1]; } view, *V = &view;
static struct {
 int cell_row, cw, ch, cols, per_atlas, cells, atlases;
 unsigned cell_key;
 bool compact;
 int cell[MAX_CELLS];
} R;
''' + src[a:b] + r'''
int main(void) {
 shelf_box box[] = {{16}, {25}};
 for (int empty = 0; empty < 2; empty++) {
  for (int atlases = 1; atlases <= 2; atlases++) {
   R.cell_row = -1; R.atlases = atlases;
   V->layout++;
   V->rows[0] = (shelf_row){empty ? 0 : 2, box, empty ? 0 : 180, .8f};
   cells_for(0);
   assert(R.cw >= K && R.ch >= K);
   assert(R.per_atlas > 0 && R.cells > 0 && R.cells <= MAX_CELLS);
   assert(R.cell_row == 0 && R.cell_key == V->layout);
   cells_for(0); // cached dimensions remain valid
  }
 }
 shelf_box thick[1024];for(int i=0;i<1024;i++)thick[i].t=i==512?300:10.5f;
 R.cell_row=-1;R.atlases=1;V->layout++;
 V->rows[0]=(shelf_row){1024,thick,125,1};cells_for(0);
 assert(R.compact && R.cells>=ROOM_BOXES+2); // natural 160x250 grid has only six cells
 puts("shelf spine cells: populated/empty Favorites and one-atlas thick-box capacity PASS");
}
''')
    exe = Path(td) / 'cells'
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=undefined', '-fno-sanitize-recover=all',
                    str(p), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
