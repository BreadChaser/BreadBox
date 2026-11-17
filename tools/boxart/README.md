# Box art name table

`make_table.py` builds `assets/romfs/covers/names.txt` and `serials.idx`, which the app
uses to find the right picture for a disc whatever its file is called
(`include/breadbox/boxart.h`).

Inputs (download them again when you rebuild; they are not kept in the repository):

```
curl -L -o psx.dat 'https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/redump/Sony%20-%20PlayStation.dat'
curl -L -o boxarts.html 'https://thumbnails.libretro.com/Sony%20-%20PlayStation/Named_Boxarts/'
python3 tools/boxart/make_table.py --dat psx.dat --listing boxarts.html --out assets/romfs/covers
```

Rebuild now and then: new pictures appear on the server and the app only asks for
names it finds in the table. (A game the table does not know is still asked for by its
file name, so an old table costs nothing for games that were already named right.)

`match_report.py` measures the match rate on real disc folders (needs the host test
binary from `tests/app/boxart/check.c`, see its header). Tests: `bash tests/app/boxart/run.sh`
and `python3 tests/app/covers/worker.py`.
