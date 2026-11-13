# GBA (and PS1) box spine sources for runtime fetch

Research 2026-10-30 (web only). Goal: real retail GBA box spines that BreadBox can download at runtime (like covers;
never shipped), for the 3D bookcase. Example game: The Legend of Zelda: The Minish Cap, NA, AGB-BZME-USA.

Rough universe: about 1,000 NA GBA boxes (a LaunchBox forum set counts 986 NTSC boxes incl. 38 bundles),
about 800 JP and 900 EU. Percentages below are against those numbers.

## Summary table

| Source | What it has | GBA spine coverage | Res / format | Lookup | Terms | Runtime spines? |
|---|---|---|---|---|---|---|
| **LaunchBox Games DB** | Box - Front/Back/**Spine**/3D, Cart, etc. (no "Box - Full" for GBA) | **296 games**: NA 202 (~20%), JP 114 (~14%), EU 68 (~7%), World 20. Box - 3D (real side art visible): NA 1,141, JP 566, EU 145 | spines ~120-310 x 980-1,550 px, JPG/PNG | Offline: daily `Metadata.zip` (title, DatabaseID, image file names). Runtime: `https://images.launchbox-app.com/<file>` | No published terms or API; owner said Metadata.zip is the intended way in; robots.txt has no Disallow | **Partly** (best real-spine source; low GBA coverage; mixed quality) |
| **ScreenScraper** | `box-2D`, `box-2D-side` (spine), `box-2D-back`, `box-texture` (flat wrap), `box-scan`, `box-3D`, `support-2D` | **Unknown** (needs dev account to count); RomM found real gaps for sides. Its GBA `box-texture` is sourced partly from The Cover Project (fan DS-case wraps) | PNG/JPG, server-side `maxwidth/maxheight` resize | By CRC/MD5/SHA1 + size, or rom name; `jeuid` + `media=box-2D-side(us)` | Free apps only; dev id/password via forum; per-user account optional; quotas per minute/day (about 20k/day free member, 1 thread); CC BY-NC-SA 4.0 | **Partly** (good API, unknown coverage, needs dev keys + users' quota) |
| **libretro-thumbnails** | Named_Boxarts (fronts only), Named_Snaps, Named_Titles, Named_Logos | **0 spines** (5,961 boxarts: USA 2,344, EU 1,162, JP 2,409) | max 512 px wide, PNG | No-Intro name | No licence file; README just credits contributors | **No** (fronts only; already our cover source) |
| **TheGamesDB** | boxart `front` / `back` only, fanart, banner, screenshot, clearlogo, titlescreen | **0 spines** (no side type in the code) | original/large/medium/thumb, JPG/PNG | API by name/id; `cdn.thegamesdb.net/images/original/boxart/<side>/<id>-1.<ext>` | Key via forum; public key = per-IP monthly allowance (was 1,000/month in 2018) | **No** |
| **GameTDB** | DS/3DS/Wii/WiiU/GC/Switch/PS3 covers incl. full covers | **No GBA** (`/GBA` redirects to 404), no PS1 | - | Game ID | "Please do not use without permission" on websites; contact them | **No** |
| **The Cover Project** | Printable full wraps (front+spine+back) | GBA cat_id=13: 2,341 covers, but all seen are **custom "DS Case - US" wraps**, not real GBA boxes | ~300 dpi JPG | `view.php?game_id=N`, `view.php?cover_id=N` | Behind a Cloudflare JS challenge (curl and WebFetch got 403) | **No** (fan-made + bot-blocked) |
| **archive.org: tabor62gb "GBA Box Scans"** (collection `game-boy-box-art`) | Unfolded flat box scans (front scan includes one side, back scan includes the title spine + flaps) | 373 items: USA ~197, JPN ~70, EU-ish ~60 | 600 dpi JPG, ~3,500x4,600, 4 MB each | Search API by serial in title (`AGB-AHNE USA`) | Uploader asks no use in ads/paywalls/repro boxes "without my permission" | **No for runtime** (needs per-scan crop; huge files); good reference/manual source |
| archive.org: "*-gba-hiresscans" / `*-boxscans` | 1,200 dpi raw box scans (front/back, some flats) | ~11 items | 12-400 MB TIFF/JPG | Item id | Per uploader | No |
| archive.org: "Nintendo Game Boy Advance Box 3D" (HyperSpin set) | 3D renders | 1,267 renders | 582x889 PNG | No-Intro name | Unknown | No (side is a **generic template**: "GAME BOY ADVANCE" only, no title) |
| MobyGames API | Covers incl. "Spine/Sides" group | Unknown for GBA | full size on Silver tier | game id/platform | Paid ($9.99/mo hobbyist, non-commercial; key cannot be shared with users) | No |

## Details

### LaunchBox Games Database (best real-spine source)
- Checked the live `Metadata.zip` (109 MB, `Metadata.xml` 517 MB, updated daily). Platform "Nintendo Game Boy Advance":
  2,378 entries (incl. GBA Video, unlicensed, homebrew). Image types per game: Box - Front 2,090, Box - 3D 1,684,
  Cart - Front 1,386, Box - Back 1,349, **Box - Spine 296**. No "Box - Full" for GBA.
- Spine regions: North America 202, Japan 114, Europe 68, World 20, others 8.
- Quality spot check (8 random spines): 5 are the real title spine; 2 EU entries are the legal/CE side panel; 1 is a
  tiny landscape crop (695x170). So validate: portrait aspect about 1:4 to 1:7, and prefer NA/JP.
- **Box - 3D** renders (1,016x998 PNG): all 16 random NA samples show the game's real side art (title on the side),
  unlike the HyperSpin template set. A 3DS bookcase spine is only ~10-20 px wide, so the side face of a 3D render
  (about 130 px wide, slight perspective) would be enough as a fallback, but it needs per-image corner detection;
  do that offline, not on the console.
- Lookup: no hash or serial in the data. Match by title: `<Name>` + `<GameAlternateName>` (with region) + platform.
  `Files.xml` lists 1,129 GBA No-Intro file names but only maps them to names. Do it like
  `tools/boxart/make_table.py`: build a table offline (No-Intro name/serial -> image file name) and ship only that table.
- Example: Minish Cap = DatabaseID 5954.
  - Spine NA: `https://images.launchbox-app.com/9a4a8f6b-c6e0-4b40-a9ff-37ce59a09a4a.jpg` (186x978, real red spine)
  - Spine JP: `https://images.launchbox-app.com/8bb6586c-ec7d-476d-9eec-e1705930ac5c.jpg` (247x1,012)
  - Spine EU: `https://images.launchbox-app.com/58f4ba6f-8fa5-4e14-aee7-90965cbbe1db.jpg` (271x1,476)
  - 3D NA: `https://images.launchbox-app.com/r2_9a35263e-1d4c-48b0-8fcf-9686f03cb7b4.png`
- Hosting: Cloudflare, `cache-control: public, max-age=31536000, immutable`. A plain UA like `BreadBox/1.0` gets 200;
  the `Python-urllib` UA got 403 (bot rule).
- TLS: the host only offers an **ECDSA** certificate chained to **GTS Root R4** (Google Trust Services).
  BreadBox's `assets/romfs/certs.pem` has ISRG X1/X2, Sectigo, USERTrust only, so add GTS Root R4 (curl+mbedTLS
  handles ECDSA; verify on hardware).
- Terms: no terms page or API exists (404 on /terms; "no official API" per their own feedback board). Jason Carr
  (developer) pointed people to Metadata.zip and said images download straight from the site; RomM, launchbox-to-json and
  others use it. Unverified permission for a public app: **ask Unbroken Software** and credit "LaunchBox Games Database".
- PS1 ("Sony Playstation", 4,651 entries): **Box - Spine 2,364 games** (JP 1,726, NA 1,662, EU 1,254), Box - 3D 4,004.
  PS1 spine coverage is far better than GBA.

### ScreenScraper (screenscraper.fr)
- Media ids (API v2 docs): `box-2D` (front), `box-2D-side` ("Boitier : Tranche" = spine), `box-2D-back`,
  `box-texture` (flat wrap), `box-scan`, `support-texture`, `support-scan`; jeuInfos also returns `box-3D`, `support-2D`.
- Lookup by ROM hash: `jeuInfos.php?devid=..&devpassword=..&softname=..&ssid=..&sspassword=..&output=json&systemeid=12&romtype=rom&crc=<crc32>&romnom=<file>&romtaille=<bytes>`
  (GBA = system 12). Then `mediaJeu.php?...&systemeid=12&jeuid=3751&media=box-2D-side(us)&maxheight=256&outputformat=png`.
  Returns `NOMEDIA` if missing, or `CRCOK` if your cached copy's hash matches.
- Minish Cap = jeuid 3751 (`https://www.screenscraper.fr/gameinfos.php?plateforme=12&gameid=3751`).
- Coverage of `box-2D-side` for GBA could not be measured: the site shows media only to logged-in users, direct
  `/medias/12/3751/...` gives 403, and the API needs dev credentials. The site credits The Cover Project, gbatemp
  and HyperSpin for "Boxes Texture", so GBA wraps can be fan DS-case covers.
- Terms: API only in "entièrement gratuites" (fully free) apps; dev credentials after presenting the app on the forum;
  quota per minute and per day per account and/or IP (about 20k/day free member, 1 thread; donors 50k-100k);
  the client must honour quota fields (`maxrequestspermin`, `maxrequestsperday`). Content CC BY-NC-SA 4.0.
  Sharing one account between users risks a ban, so each user would need their own login (or anonymous 1-thread access,
  which can be closed when servers are busy).
- TLS: ECDSA only (ISRG Root X2, already in our certs.pem).

### libretro-thumbnails
- `Nintendo_-_Game_Boy_Advance`: Named_Boxarts 5,961, Named_Snaps 5,803, Named_Titles 5,087, Named_Logos 3,037.
  Boxarts are fronts only (the silver "GAME BOY ADVANCE" strip is part of the NA front, not the spine), max 512 px
  wide PNG. No spine/full variants. No licence file; README credits many sources.
- URL: `https://thumbnails.libretro.com/Nintendo%20-%20Game%20Boy%20Advance/Named_Boxarts/Legend%20of%20Zelda%2C%20The%20-%20The%20Minish%20Cap%20%28USA%29.png` (200, 512x512).
- Keep it as the front source; it is not a spine source.

### TheGamesDB
- Code (TheGamesDBv2) only knows image types fanart, banner, boxart (side `front`/`back`), screenshot, titlescreen,
  icon, clearlogo. No spine.
- Minish Cap id 5954: `https://cdn.thegamesdb.net/images/original/boxart/front/5954-1.png`, `.../boxart/back/5954-1.jpg`.
- Keys via forum; public key has a per-IP monthly allowance; private key must never ship in an app.

### GameTDB
- Platforms: 3DS, DS, Switch, Wii U, Wii and GameCube, PS3. No GBA, no PS1. DS/3DS "coverfull" wraps exist
  (`https://art.gametdb.com/ds/coverfullHQ/US/<ID4>.jpg`) but DS boxes are not GBA boxes.
- FAQ: "Please do not use without permission if you wish to use this file on a website"; they ask apps to link back
  and contact them.

### The Cover Project
- GBA category `view.php?cat_id=13` ("Gameboy Advance Game Covers", 2,341 covers). Every GBA cover found says
  "Custom Cover" and "Case Type: DS Case - US" (made to put GBA carts in DS keepcases): DS proportions, invented
  spine. How to tell: the cover page shows "Custom Cover" and the Case Type; real GBA boxes are cardboard and have no
  keepcase wrap at all, so a wrap with a DS-style spine is fan-made by definition.
- Site sits behind a Cloudflare managed JS challenge (403 for curl and WebFetch), so a 3DS cannot fetch from it.
- PS1 entries there are mostly real jewel-case scans (with spines), but the challenge blocks those too.

### archive.org
- **tabor62gb's GBA Box Scans** (collection `game-boy-box-art`, 373 items, titles like
  `SpyHunter [AGB-AHNE USA] Box Scan`). Checked SpyHunter: "1.5 Box Front Scan.jpg" (3,515x4,632, 600 dpi) is the
  unfolded front with one side panel and flaps; "2.5 Box Back Scan.jpg" contains the title spine. Real spines, but they
  need per-scan cropping and are 4 MB each. Lookup by serial is possible:
  `https://archive.org/advancedsearch.php?q=collection:game-boy-box-art+AND+title:"AGB-BZME"&fl[]=identifier&output=json`
  then `https://archive.org/download/<id>/2.5%20Box%20Back%20Scan.jpg`. Minish Cap is **not** in it (none of 40 sampled
  items had separate side files either). Uploader's disclaimer: preservation only, asks not to be used in
  ads/premium/paywall/repro products without permission.
- `*-gba-hiresscans` (about 11 items, 1,200 dpi, e.g. `pokemon-emerald-version-gba-hiresscans`), `AGB-AMAE-USA-boxscans`:
  huge TIFF/JPG, front/back.
- `nintendo-game-boy-advance-box-3d` (1,267 renders): generic side template, not real spines.
  `gbahsbox` (HyperSpin 2D fronts).
- archive.org download hosts (`iaXXXXXX.us.archive.org`) did not complete a TLS 1.2 handshake with any cipher tried
  from here; test on hardware before relying on it.

### Others
- MobyGames: has a "Spine/Sides" cover group, but the API is paid and keyed per developer; not usable per user.
- VGCollect / Gamesdatabase.org: user photos and fronts; no API.

## Recommendation
- **Primary: LaunchBox Games DB.** Build an offline table (like `tools/boxart/make_table.py`) from Metadata.zip:
  No-Intro name (and serial via our GBA dat) -> LaunchBox `Box - Spine` file name, region-matched, aspect-checked;
  ship only the table; fetch `https://images.launchbox-app.com/<file>` at runtime. Add GTS Root R4 to certs.pem.
  Ask Unbroken Software for permission and credit them. Expect real spines for ~20% of NA GBA games (much more for PS1).
- **Fallback: generated spine** from the libretro front we already fetch (system-style band + title/logo crop), so
  every game gets a spine. Optional second fallback later: side faces cropped offline from LaunchBox `Box - 3D`
  (covers ~1,100 NA games) if the owner wants more real spines.
- ScreenScraper is worth a dev-account request only to measure `box-2D-side` coverage; per-user accounts and quotas
  make it a poor default.

## Sources
- LaunchBox Metadata.zip: https://gamesdb.launchbox-app.com/Metadata.zip
- LaunchBox "no official API" request: https://feedback.launchbox-app.com/p/public-api-access-for-the-launchbox-games-database-globewithmeridianswrench
- LaunchBox external scraper thread: https://forums.launchbox-app.com/topic/30123-launchbox-games-db-external-scraper/
- Skyscraper issue about LaunchBox DB: https://github.com/muldjord/skyscraper/issues/132
- RomM PR 5129 (LaunchBox spine/back/3D): https://github.com/rommapp/romm/pull/5129
- LaunchBox forum GBA fronts set (986 NTSC boxes): https://forums.launchbox-app.com/files/file/2720-abeezys-nintendo-game-boy-advance-box-fronts/
- ScreenScraper API v2: https://www.screenscraper.fr/webapi2.php
- ScreenScraper Minish Cap: https://www.screenscraper.fr/gameinfos.php?plateforme=12&gameid=3751
- ScreenScraper quota discussion: https://github.com/rommapp/romm/issues/3978 , https://github.com/rommapp/romm/pull/3989
- Skyscraper scraping modules: https://gemba.github.io/skyscraper/SCRAPINGMODULES/
- libretro-thumbnails: https://github.com/libretro-thumbnails/libretro-thumbnails , https://github.com/libretro-thumbnails/Nintendo_-_Game_Boy_Advance
- TheGamesDB source: https://github.com/TheGamesDB/TheGamesDBv2 ; key info https://github.com/TheGamesDB/TheGamesDBv2/blob/master/API/key.php ; allowance https://forums.thegamesdb.net/viewtopic.php?t=60
- TheGamesDB Minish Cap: https://thegamesdb.net/game.php?id=5954
- GameTDB: https://www.gametdb.com/ , FAQ https://www.gametdb.com/Main/FAQ
- The Cover Project GBA: https://thecoverproject.net/view.php?cat_id=13 ; example https://about.thecoverproject.net/view.php?game_id=1117
- archive.org tabor62gb collection: https://archive.org/details/game-boy-box-art ; example https://archive.org/details/spyhunter-usa-box
- archive.org 3D set: https://archive.org/details/nintendo-game-boy-advance-box-3d
- MobyGames API: https://www.mobygames.com/info/api/ , https://www.mobygames.com/api/subscribe/
