# Moving from PSX3D

BreadBox is the successor to PSX3D. It plays everything PSX3D did, and on its
first start it copies your PSX3D files so you can carry on where you were.

## What happens on first start

BreadBox looks in `sdmc:/3ds/PSX3D/` and copies:

- memory cards, including shared cards;
- save states and Continue, with their pictures (BreadBox calls them postcards);
- per-game settings;
- cheats: the ones you switched on and your own code files;
- BIOS files;
- your play history.

If BreadBox has no settings of its own yet, it also takes PSX3D's app settings.
A notice says how many files came over.

It is a one-way copy. PSX3D's folder is never changed, and what you play in
BreadBox does not go back to PSX3D. Both apps can stay installed side by side.

## Save slot numbers

PSX3D numbered its save slots from 0. BreadBox numbers postcards from 1, so PSX3D
slot 0 is postcard 1, slot 1 is postcard 2, and so on.

## When both apps have a file

If a save exists in both apps, BreadBox keeps its own and stores PSX3D's copy in
`sdmc:/3ds/BreadBox/import/psx3d/`. For save states with known save times, the
newer one wins and the other goes into that game's backups. Nothing is thrown
away, so you may want to clean up that folder by hand once you are sure you have
what you need.

## Importing again

**Settings → Collection → Import again from PSX3D** copies anything new or changed
since the last time. It does not copy app settings again.

## What is not copied

Developer switches and passwords, and anything outside `sdmc:/3ds/PSX3D/`. Your
games stay where they are; pick their folder in **Settings → Collection → Games
folder** if BreadBox does not find them by itself.
