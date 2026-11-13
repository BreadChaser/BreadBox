# GBA retail game box: dimensions and spine/side layout (research, 2026-10-30)

Web research only. Every number has a source (list at the end, [S#]).
"Measured from scan" means I measured panel sizes off a 600 DPI flat scan
of an unfolded box, read from the image at display scale, so allow about ±0.5–1 mm.

## TL;DR

| Region | Outer box W x H x D (mm) | Orientation | Best value for the model |
|---|---|---|---|
| NA (USA/Canada) | 124–125 x 124–125 x 22.5–25 | square, portrait use | **125 x 125 x 23** |
| EU (PAL) | 125 x 125 x 23–25 | square, same die size as NA | **125 x 125 x 23** (same as NA) |
| JP | 135–137 x 84–87.5 x 21–21.3 | landscape (wide) | **137 x 87 x 21** |

- Depth/face ratio from real side scans is about 0.184. With a 125 mm face that gives a 23 mm side.
- NA spine: there is **no standard spine design**. The left side panel carries on the cover art or color, with the game logo in the upper half and the publisher logo (Nintendo red oval etc.) at the bottom. It has no GBA wordmark and no ESRB box.
- Warning: the "black band with white GAME BOY ADVANCE logo at the top" spines in LaunchBox and TheCoverProject are **replacement-cover spines for DS-style cases**, not retail boxes (details in section 3.4).

---

## 1. North American box dimensions

### 1.1 Direct measurements of the box (best evidence)

| Source | W x H x D (mm) | Notes |
|---|---|---|
| gabuz0me gb-box-template (GitHub) [S1] | 125 x 125 x 25 | "I measured one and found 125x125x25 mm." Same template used for GB/GBC/GBA. |
| circuit-board.de forum size list [S2] | 125 x 125 x 25 | Listed for Western GBA, GBC and GB boxes alike. |
| nintendofans.de forum [S3] | ~130 x 130 x 25 | Rough "about" figure, older thread. Least reliable. |
| FF Tactics Advance USA, 600 DPI flat scan (archive.org) [S4] | ~124 x 124, side panel ~23, top panel ~23–24 | Measured by me. Front panel ≈1267 px at display scale (factor 2.31) gives 2927 px at 600 DPI = 123.9 mm. Left side panel ≈ 22.8 mm. Flat card between creases, so the folded box is a hair bigger. |
| Metroid Fusion USA side scan (LaunchBox) [S5] | side ratio 1077 / 5823 = 0.185 | Front scan of the same box is 5882 px square, so side depth = 0.183 x face, about **22.9 mm** for a 125 mm face. |
| Super Mario Advance USA side scan (LaunchBox) [S6] | ratio 175 / 950 = 0.184 | About 23.0 mm for a 125 mm face. |

### 1.2 Box protector listings (an upper bound; protector is box + ~1–3 mm)

| Protector | Listed size (mm) | Inner/outer? |
|---|---|---|
| Hand Held Legend / Malko PET 0.35 mm [S7] | 125 x 125 x 23 | Not stated. Thin PET, so inner ≈ outer. |
| Pacsam (Canada) [S8] | 126 x 126 x 23 | Not stated. |
| GameBoxProtector "IMPERFECT" GB/GBA [S9] | 126 x 126 x 24 | Seller says "slightly too tight", tight with thick manuals. So the box is just under this. |
| GameBoxProtector GBA (PAL) [S10] | 127 x 127 x 25 | Correct-fit version. |
| EVORETRO PET 0.40 mm [S11] | 128 x 128 x 25 | Not stated. |
| RetroProtection GB/VB/GBA/GBC, 0.35 mm PET [S12] | 5.0625 x 1 x 5.0625 in = 128.6 x 25.4 x 128.6 | Not stated. Fits NTSC GB/GBC/GBA/VB. |
| Kollector Protector acrylic [S13] | interior 5.1 x 1.1 x 5.1 in = 129.5 x 27.9 x 129.5 | Interior. 4 mm acrylic. |
| BSAcrylics acrylic [S14] | interior 130 x 28 x 130 | Interior. |
| EVORETRO acrylic 4 mm [S11b] | 130 x 130 x 29 | Probably outer. |
| XYAB [S15] | 5 x 5 x 1.25 in | Assembled. |

Snug PET protectors (126 tight, 127 OK) put the box face at about 124.5–125.5 mm.

### 1.3 Best NA value and spread
- **Face: 125 mm** (spread 123.9–125; protectors say under 126). 124.5 is equally defensible.
- **Depth: 23 mm** (spread 22.8–25). The scans give 22.8–23.0. The 25 mm hand measurements likely include bulge from the manuals, or round up. Protectors use 23–25.
- Very few boxes are non-standard: WarioWare: Twisted! has its own protector [S12b], and Pokémon/GBA Video boxes come in a taller 104 x 145 x 23 mm format [S16]. Ignore these for the generic model.

### 1.4 Construction (NA, from the flat scans [S4])
- A four-panel tube (front, left side, back, right side) in thin printed card (gray/kraft inside). The left side panel hinges off the front's left edge.
- **Top**: a printed top panel hinges off the **back** and ends in a rounded **tuck flap**, so the box opens at the top. The side panels have small dust flaps.
- **Bottom**: closure flaps with notched lock tabs, normally left closed. gabuz0me notes "some boxes have self-locking Y-shaped bottom flaps" [S1].
- The side-panel dust flaps carry the game code "AGB-AFXE USA" plus a print/date code ("031027", "5") [S4].
- **Inner tray**: a folded cardboard insert with a cartridge well. The manual and leaflets sit beside or under it. Listed sizes: 120 x 120 x 18 mm [S17], 121 x 121 x 22 mm [S1], 12 x 12 x 2 cm [S18]. NA trays don't fit PAL/JP boxes [S19].

## 2. EU (PAL) and JP boxes

### 2.1 EU
- Same square die as NA: 125 x 125 x 25 [S2]. The EU side scans in LaunchBox are 271–272 x 1476 px at 300 DPI [S20], which is **125.0 mm long x 22.9–23.0 mm deep**. The EU fronts are 1477 x 1476 px, also 125 mm. The PAL protector is 127 x 127 x 25 [S10]. One protector fits all EU GB/GBC/GBA boxes [S21].
- Caveat: the EU scans may have been normalized to one size by a single scan project (same file-naming scheme), but 125.0 mm matches everything else.
- Logo/flap placement on EU boxes varies more. A ResetEra poster says EU boxes have a big CE label on the side, and logo placement varies (top or either side), so they don't stack neatly; NA boxes always have it on one side [S22].

### 2.2 JP
| Source | W x H x D (mm) |
|---|---|
| nantoka-game.com, digital caliper [S23] | **137.1 x 87.3 x 21.3** |
| circuit-board.de [S2] | 136 x 86 x 21 |
| 3A Company case listing (via search snippet; page 403) [S24] | ~136 x 87 x 21 (their case is 137 x 88.5 x 22) |
| YouTube "GBA (JAP) Game Box Dimensions" [S25] | 135 x 84 x 21 |
| GameBoxProtector JPN protector [S26] | 138 x 89 x 23 |
| RetroProtection JP GBA protector [S27] | 5.4375 x 3.5625 x 0.9375 in = 138.1 x 90.5 x 23.8 |
| Super Robot Taisen OG JP 600 DPI flat scan [S28], measured by me | front ≈ 137 wide, long top panel ≈ 21.3 deep |

- **Best JP: 137 x 87 x 21 mm**, landscape (wider than tall) [S22][S23].
- **JP construction is different** (scans [S28][S29]). The **long** top and bottom panels are fixed and printed. The **short ends** are the tuck-flap closures, so the box opens from a short side. The back's recycling marks say 紙 (paper) for the outer box and paper tray, and プラ / PE for the poly bag. So a JP box holds a paper tray plus a cart in a PE bag [S30].

## 3. Spine, sides and front layout

Fractions run along the panel's long axis, top of the box = 0%.

### 3.1 NA front cover (what the spine generator samples)
- A **silver/gray metallic vertical band down the left edge** takes about 22–23% of the width (≈28 mm, measured on [S4]; a ResetEra poster says "about 1/6" [S22]). It has a light-to-dark horizontal gradient, darkest at the right edge where it meets the art.
- In the band is the "GAME BOY ADVANCE" wordmark, **rotated to read bottom-to-top**: "GAME BOY" in outlined silver/white, "ADVANCE" in black, spanning roughly 10%–98% of the height.
- At the top of the band is a small **indigo "ONLY FOR" corner triangle**, about the top 10–15%. Some boxes have a centered logo without the triangle [S31].
- IGN (2001) said Nintendo added the silver strip so buyers wouldn't confuse GBA and GBC games [S31]. GamesRadar says UK logos "had a colorful outline instead of the metallic letters" used in the US [S32].
- The art fills the remaining ~77%, with the title logo usually at the top or center. The ESRB rating box sits at the bottom-right or bottom-left of the art (FFTA and Ruby bottom-right; Castlevania bottom-left next to the band). The gold "Official Nintendo Seal of Quality" is at the bottom-left of the art (third-party and some first-party), and the publisher logo is at the bottom (center or right) [S4][S6][S33].
- Wordmark fonts: modified Gill Sans MT Italic ("GAME BOY") and modified Bank Gothic ("ADVANCE") [S34].

### 3.2 NA sides (real scans: Metroid Fusion [S5], Super Mario Advance [S6], FF Tactics Advance [S4], Castlevania AoS and Pokémon Ruby 3D renders [S33][S35])
- **Left side panel (next to the silver band) = the de facto spine.** Its background continues the cover art or uses a flat cover color: blue (Metroid), sky (SMA), pale white-blue (FFTA), red (Ruby), gray art (Castlevania).
- Text runs **top-to-bottom**, rotated 90° clockwise so the letter tops face the front cover.
- Typical Nintendo-published layout:
  - 0–8%: plain background.
  - **~8–60%: game logo** (Metroid 15–38%, SMA 5–40%, FFTA 10–60%, Ruby 8–40%).
  - Middle: a character cut-out from the cover (Metroid 45–75%, Mudkip on Ruby).
  - **~84–98%: red-outlined "Nintendo" oval logo.** On Ruby, "The Pokémon Company" sits at ~62–85%.
- Third parties vary. Konami's Castlevania puts the **Konami logo at the top (~3–12%)** and the game logo in the lower half (~50–95%) [S33].
- **No GBA wordmark, no ESRB box and no seal on the spine.** No fixed color convention: the color comes from the cover.
- **Right side panel**: notices, not art. On Nintendo-published boxes: "FOR CUSTOMER SERVICE VISIT WWW.NINTENDO.COM" red box, "IMPORTANT! read the instruction booklets" red box, the photosensitive-seizure WARNING box, "For sale, rental and use only in USA, Canada, Mexico and Latin America", and recycling text, all rotated [S4].
- **Top panel** (opening flap): game logo, publisher address (e.g. "Nintendo of America Inc., P.O. Box 957, Redmond, WA..."), "MADE IN JAPAN" [S4].
- **Bottom**: only the unprinted lock flaps. The tuck/dust flaps carry the AGB-XXXX-USA code and a print code [S4].
- **Back panel**: game logo top-left; the "ONLY FOR [GAME BOY ADVANCE]" black box plus link-cable icon top-right. **ESRB rating with content descriptors, the AGB P XXXE code and the UPC barcode at the bottom-left**. Gold Official Nintendo Seal plus its text and the publisher logo at the bottom-center. Legal text at the bottom-right [S4][S36].
- Player's Choice reprints exist (e.g. Pokémon LeafGreen) [S37]. On the older Game Boy line, Player's Choice turned the silver logo gold [S32]; I did not verify a GBA equivalent.

### 3.3 EU front and sides (real scans: SMA EUR AGB-AMAP-NEU6 [S20], Castlevania EUR AGB-A2CP-EUR [S38], Metroid Fusion EU/FR [S39])
- **Front**: the same left vertical band, but the EU band is **indigo/purple-gray**, with the white "GAME BOY ADVANCE" wordmark reading bottom-to-top. There is no "ONLY FOR" triangle. A small round "Original Nintendo Seal of Quality" sits at the bottom-right ("Original", not "Official"), with the Nintendo or publisher logo at the bottom [S39].
- **Left side panel = regulatory panel**:
  - **0–55%: white box** with a large **CE mark**, the Nintendo logo, "D-63760 Großostheim" and "PLEASE RETAIN THE PACKAGING" in about 10 languages.
  - Below it, art and/or the game logo (Castlevania logo 72–98%).
  - At the bottom (SMA ~78–98%), the **age-rating box**: ELSPA "3+" on early UK/EU boxes, with PEGI on later releases (PEGI is not shown in my samples).
- **Back**: multilingual text blocks with flags (6 languages), a GBA / GBC / GB compatibility tick table at the top, "AGB P XXXP" code plus EAN barcode at the bottom-left, and the round Original seal at the bottom-center [S39b].

### 3.4 Not retail: the "black spine with white GBA logo" style
- LaunchBox's NA "Box - Spine" for Castlevania AoS (187 x 1370) [S40] has a **black top band (~0–27%) with a white stacked "GAME BOY / ADVANCE" logo** and the title below it.
- Its metadata shows it was cut from a 3260 x 1370 InDesign proof. That is the TheCoverProject / supermediastore GBA **DS-case slipcover template**: 1535 + 190 + 1535 px, where the guide says to "paste the GBA Spine Logo into the image and align it to the very top" [S41].
- Fans print these to keep GBA carts in DS cases [S42]. Fine as a stylized look, but a retail-accurate model should use section 3.2.

### 3.5 JP (scans: Super Robot Taisen OG front [S28], Super Robot Taisen A back [S29], Metroid Fusion JP back [S30]; renders [S43])
- **Front (landscape)**: top-left has the "GAME BOY ADVANCE" wordmark with **"ゲームボーイ アドバンス" katakana** under it. Publisher logo top-right or bottom-right. Title in Japanese, plus a genre/players line (e.g. "1人用 シミュレーションゲーム"). CERO rating at the bottom-right on later releases. There is no left band, and the art fills the whole front [S22][S28][S43].
- **Top long panel** (137 x 21): black on the SRT OG box. GBA wordmark at the left (~5–40% of the length), publisher logo under it, game logo at the right (~65–95%). The dust flap carries "AGB-AOGJ-JPN" [S28].
- **Bottom long panel**: game logo, katakana "ゲームボーイ アドバンス" and publisher logo. The dust flap carries "AGB-ASRJ-JPN" [S29].
- **Short end panels** (87 x 21, these open):
  - One end is just a large **"GAME BOY ADVANCE" wordmark running the full length** (~8–92%).
  - The other end has publisher logo top, "MADE IN JAPAN", game logo, and "AGB-P-ASRJ (JPN)" at the bottom [S29].
  - First-party renders show the same GBA-wordmark end [S43].
- **Back**: story text and screenshots at the top. At the bottom-left, "本品は○印のついている機器に使用できます" with a ○/× compatibility table (GBA / GBC / GB / SGB). At the bottom-right, "FOR SALE AND USE IN JAPAN ONLY", "日本国内専用", the JAN barcode (49...), and recycling marks [S29][S30].
- Code format: AGB-XXXJ-JPN [S44].

## 4. Reference images (open in a browser; real scans unless noted)
1. NA unfolded box, all 6 panels, 600 DPI (FF Tactics Advance): https://archive.org/details/final-fantasy-tactics-advance-usa-box
2. NA spine scan, high-res (Metroid Fusion): https://images.launchbox-app.com/973c59ff-0297-4983-8f52-8afff7bae227.jpg. Also SMA: https://images.launchbox-app.com/d186dbbd-8f55-4294-abe3-1a2098a8f81b.jpg
3. EU side scans (SMA, Castlevania): https://images.launchbox-app.com/446c3ef2-3323-4f7a-9d12-3a0eef58caeb.jpg and https://images.launchbox-app.com/2f8e7c9e-3220-44d2-84c4-8bd7896070ab.jpg
4. JP unfolded box (front + top panel): https://archive.org/details/super-robot-taisen-original-generation-jpn-box. Back + ends + bottom: https://archive.org/details/super-robot-taisen-a-jpn-box
5. Shelf/group photos (JP), from the ResetEra thread [S22]: https://discodracula.files.wordpress.com/2014/06/2.png and http://michibiku.com/wp-content/uploads/2016/11/DSC_0050.jpg
6. 3D renders, front + left side (not photos): NA Ruby https://images.launchbox-app.com/r2_a85edc7a-5c36-45f7-ad6c-6f40003285cd.png, NA Castlevania https://images.launchbox-app.com/r2_533b46a7-2455-4c6d-900b-9dd1dd099b91.png, EU Metroid https://images.launchbox-app.com/r2_5db5cc7d-afe1-4fc3-8511-6b8d9efd0b43.png, JP Metroid https://images.launchbox-app.com/61bf6301-69c8-4a98-9ee7-defb41ec2400.png
7. More: tabor62gb's GBA box scans on archive.org (many NA/JP titles, 600 DPI, unfolded), e.g. https://archive.org/details/eragon-usa-box

Gap: I found no good verified photo of NA GBA boxes on a shelf spine-out. Collectors mostly display them face-out or in protectors [S45]. The unfolded scans (item 1) are the better reference.

## Sources
- S1 https://github.com/gabuz0me/gb-box-template
- S2 https://circuit-board.de/forum/index.php/Thread/9645-Abmessungen-von-OVP-Modulen-Schutzh%C3%BCllen/
- S3 https://www.nintendofans.de/forum/viewtopic.php?t=25899
- S4 https://archive.org/details/final-fantasy-tactics-advance-usa-box (files "1.5 Box Front Scan.jpg" 3495x4621 and "2.5 Box Back Scan.jpg" 3482x4622, 600 DPI JFIF)
- S5 https://gamesdb.launchbox-app.com/games/images/2497-metroid-fusion (NA spine 1077x5823, NA front 5882x5882)
- S6 https://gamesdb.launchbox-app.com/games/images/2225-super-mario-advance
- S7 https://handheldlegend.com/products/original-game-boy-game-boy-advance-game-box-protector
- S8 https://www.pacsam.ca/en/retro-game-box-protectors/
- S9 https://www.gameboxprotector.com/products/nintendo-gameboy-advance-game-box-protector (search snippet; page now 404)
- S10 https://www.gameboxprotector.com/products/nintendo-game-boy-advance-game-box-protector
- S11 https://www.amazon.com/Premium-Transparent-Protector-Compatible-EVORETRO-Nintendo/dp/B07C9C313C ; S11b https://www.amazon.com/Acrylic-Game-Protector-Compatible-Nintendo-Boy/dp/B09MYW5VH5
- S12 https://retroprotection.com/product/game-boy-virtual-boy-box-protector/ ; S12b https://retroprotection.com/product-category/video-games/nintendo/game-boy-virtual-boy/
- S13 https://kollectorprotector.com/products/nintendo-game-boy-video-game-box-hard-case-made-with-5mm-thick-acrylic-best-price-for-hard-game-cases
- S14 https://bsacrylics.com/products/game-boy-advance-acrylic-uv-resistant-video-game-case-protector
- S15 https://xyab.com/products/10-pack-gameboy-cartridge-box-protector
- S16 https://katanacollectibles.com/kc3-7102/
- S17 https://www.gameboxprotector.com/products/game-boy-advance-replacement-inlay-tray
- S18 https://www.etsy.com/listing/1040240381/nintendo-gameboy-advance-gba-inlay
- S19 https://www.amazon.com/Compatible-Game-Boy-Advance-Replacement-Protection/dp/B0FSZ52GWT
- S20 https://images.launchbox-app.com/446c3ef2-3323-4f7a-9d12-3a0eef58caeb.jpg (SMA EUR side, 271x1476 @300 DPI)
- S21 https://www.boxprotectors.nl/c-5833065/gameboy-game-acrylic/ (and the boxprotectors.nl snug-fit GB protector listing)
- S22 https://www.resetera.com/threads/japanese-gba-boxes-awesome-shame-they-are-cardboard.497781/
- S23 https://nantoka-game.com/%E3%82%B2%E3%83%BC%E3%83%A0%E3%83%9C%E3%83%BC%E3%82%A4%E3%82%A2%E3%83%89%E3%83%90%E3%83%B3%E3%82%B9%E3%81%AE%E5%A4%96%E7%AE%B1%E3%81%AE%E5%A4%A7%E3%81%8D%E3%81%95%E3%82%84%E3%82%B5%E3%82%A4%E3%82%BA/
- S24 http://www.3a-onlinestore.com/item/itemgenre/b1c7c1fcb5/124512/
- S25 https://www.youtube.com/watch?v=dRYYMZ5hajE
- S26 https://www.gameboxprotector.com/products/nintendo-game-boy-advance-jpn-game-box-protector-display-cases
- S27 https://retroprotection.com/product/japanese-game-boy-advance-box-protector/
- S28 https://archive.org/details/super-robot-taisen-original-generation-jpn-box (front 4897x2519)
- S29 https://archive.org/details/super-robot-taisen-a-jpn-box (back 4895x2503)
- S30 https://images.launchbox-app.com/7a0dffdd-8b76-459e-916c-c2ce36884176.png (Metroid Fusion JP back)
- S31 https://www.pinterest.com/pin/game-boy-advance-us-game-box-art-ign--473581717048983670/ (IGN 2001), https://emumovies.com/files/file/4749-abeezys-nintendo-game-boy-advance-box-fronts-usa/
- S32 https://www.gamesradar.com/an-extremely-interesting-history-of-game-packaging/3/
- S33 https://gamesdb.launchbox-app.com/games/images/3181-castlevania-aria-of-sorrow
- S34 https://logos.fandom.com/wiki/Game_Boy_Advance
- S35 https://gamesdb.launchbox-app.com/games/images/2241-pokmon-ruby-version
- S36 https://images.launchbox-app.com/576eff09-b6bf-4fbf-9130-87d78ad9df3d.jpg (Castlevania NA back)
- S37 https://archive.org/details/pokemon-leafgreen-version-players-choice-usa-box
- S38 https://images.launchbox-app.com/2f8e7c9e-3220-44d2-84c4-8bd7896070ab.jpg
- S39 https://images.launchbox-app.com/r2_5db5cc7d-afe1-4fc3-8511-6b8d9efd0b43.png ; S39b https://images.launchbox-app.com/c4ac57a3-fb39-4805-bf52-0e7f82c08803.jpg
- S40 https://images.launchbox-app.com/a1a671cc-3f3d-4783-86c7-039e8fd61cf8.jpg
- S41 http://blog.supermediastore.com/article/how-to-print-dvd-video-game-covers
- S42 https://gbatemp.net/threads/my-custom-gba-boxart-slipcovers-for-use-with-ds-cases.462037/ ; https://forums.launchbox-app.com/files/file/5193-nintendo-game-boy-advance-2d-front-spines/
- S43 https://images.launchbox-app.com/61bf6301-69c8-4a98-9ee7-defb41ec2400.png ; https://images.launchbox-app.com/d9929917-3808-41f6-a4f9-04d3fe50747a.png
- S44 https://www.videogamegemvault.com/blogs/video-game-gem-vault-blog/what-and-where-is-a-nintendo-product-code-for-gba-games-with-super-mario-advance-4-example
- S45 https://www.videogamesage.com/forums/topic/10391-how-dowould-you-display-your-game-boy-games/
