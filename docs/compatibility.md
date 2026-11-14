# Compatibility

What we have tested so far, and on which console. A game that isn't listed hasn't
been tried yet; tell us how yours runs (see [Tell us what you find](#tell-us-what-you-find)).

- [PlayStation](#playstation)
- [Game Boy Advance](#game-boy-advance)

## PlayStation

BreadBox's PlayStation emulator comes from PSX3D 0.5.2. The start-up checks and
player reports below were made with PSX3D. All tested discs are US (NTSC-U).

### Speed

Measured speed on real consoles, per game and per console, is in
[Performance](performance.md).

### Player reports (from PSX3D)

- Worms World Party runs slowly on Old 3DS.
- Resident Evil 2 (Spain, PAL) showed "lines on screen" for one player. We couldn't reproduce it with the US disc; a photo would help.
- Capcom vs. SNK: the flash during a super move is part of the game.

### Start-up checks (63 discs)

Each disc was started from scratch in a PC simulator for about 100 seconds of game time with the built-in BIOS. A real BIOS ran the same discs for comparison.
This checks that the game starts and gets to the scene below, not speed or sound.

- 61 of 63 reached the scene named below.
- 2 did not: **Gran Turismo** (hangs after its loading screen with the built-in BIOS) and **Mega Man Legends 2** (black after the intro).
- The built-in BIOS keeps memory-card saves intact.

| Game (US disc) | Reached |
|---|---|
| Alundra (Rev 1) | Ship gameplay. |
| Ape Escape | Menus. Shows a "digital controller" warning until the controller is set to Analog; analog play not checked. |
| Blood Omen - Legacy of Kain | Opening tavern gameplay. |
| Brave Fencer Musashi | Opening forest gameplay. |
| CTR - Crash Team Racing | Adventure hub; a new save was created and loaded again (card data unchanged). |
| Castlevania - Symphony of the Night | Richter prologue gameplay. |
| Chrono Cross (Disc 1) | Name-entry menu. |
| Crash Bandicoot | N. Sanity Beach gameplay. |
| Crash Bandicoot - Warped | Warp-room opening; later gameplay not checked. |
| Crash Bandicoot 2 - Cortex Strikes Back | Warp-room gameplay. |
| Diablo (En,Fr,De,Sv) | Tristram gameplay. |
| Digimon World | Player-name entry. |
| Dino Crisis (Rev 1) | Opening playable sequence. |
| Driver - You Are the Wheelman (Rev 1) | Garage driving test. |
| Driver 2 (Disc 1) (Rev 1) | Street gameplay; missions not checked. |
| Einhaender | First-stage gameplay. |
| Final Fantasy Anthology - Final Fantasy VI (Rev 1) | Opening story after New Game; gameplay not checked. |
| Final Fantasy Chronicles - Chrono Trigger (Rev 1) | Title/load menu. |
| Final Fantasy IX (Disc 1) (Rev 1) | Empty-card load menu. |
| Final Fantasy Origins (Rev 1) | Game-selection title and the first game's load menu. |
| Final Fantasy Tactics | Name-entry menu. |
| Final Fantasy VII (Disc 1) | Title/New Game/Continue menu. |
| Final Fantasy VIII (Disc 1) | Empty-card load menu. |
| Gran Turismo | **Hangs** after its loading screen (black picture). Works with a real BIOS. |
| Gran Turismo 2 (Arcade Mode) (Rev 1) | Arcade race gameplay. |
| Grandia (Disc 1) | Empty-card load menu. |
| Harvest Moon - Back to Nature | Load-diary menu; saving not checked. |
| Klonoa - Door to Phantomile | Name entry and controller-settings menu. |
| Legacy of Kain - Soul Reaver | Opening sequence; control not checked. |
| Legend of Dragoon, The (Disc 1) | Title and opening town/movie; gameplay not checked. |
| Legend of Legaia | Title and opening story; gameplay not reached. |
| Medal of Honor | Opening forest-mission gameplay. |
| MediEvil | Dan's Crypt gameplay/dialogue. |
| Mega Man Legends 2 | **Black screen** after the opening movie, with and without a real BIOS. |
| Mega Man X4 | Opening-stage X gameplay. |
| Metal Gear Solid (Disc 1) (Rev 1) | Title menu. |
| Need for Speed - High Stakes | Race gameplay. |
| Oddworld - Abe's Oddysee (Rev 2) | RuptureFarms gameplay. |
| Parasite Eve (Disc 1) | Empty-card slot menu. |
| R4 - Ridge Racer Type 4 | Team-selection/car-data menus. |
| Rayman | Gameplay; a save was created and loaded again. |
| Resident Evil | Mansion opening; later progress not checked. |
| Resident Evil 2 - Dual Shock Ver. (Disc 1) | Title/load menu. |
| Resident Evil 3 - Nemesis | Title/load menu. |
| Silent Hill | Opening fog-street gameplay. |
| Soul Blade (Rev 1) | Character-selection menu. |
| Spider-Man | Rooftop gameplay. |
| Spyro - Year of the Dragon (Rev 1) | Sunrise Spring gameplay. |
| Spyro 2 - Ripto's Rage! | Glimmer gameplay. |
| Spyro the Dragon | Gameplay on a fresh card; loading saved progress not checked. |
| Star Ocean - The Second Story (Disc 1) | Opening playable sequence; battles not checked. |
| Street Fighter Alpha 3 | Fight (may be the demo). |
| Suikoden II | Load menu. |
| Tekken 3 | Fight (may be the demo). |
| Tenchu - Stealth Assassins (Rev 1) | Opening mission gameplay. |
| Tomb Raider (Rev 6) | Caves gameplay. |
| Tomba! | Opening playable village/dialogue. |
| Tony Hawk's Pro Skater 2 | Hangar gameplay. |
| Twisted Metal 2 | Arena gameplay. |
| Vagrant Story | Opening playable area. |
| Wild Arms | Empty-card load menu. |
| Wipeout XL | Race gameplay. |
| Xenogears (Disc 1) | Empty-card load menu. |

## Game Boy Advance

- **Start-up checks:** 301 cartridges, each started on a 3DS emulator (Old 3DS
  model) and in a PC build of BreadBox's Game Boy Advance emulator, and compared
  with mGBA. Built-in BIOS. These ran on a test build a few days before 0.9.0;
  the fixes made after it are marked in the table.
- **Saves** (battery saves, Continue, postcards, Start over, power cut) were
  checked on 12 games covering SRAM, EEPROM 512 B and 8 KB, and Flash 64 KB and
  128 KB. The save files match the ones mGBA writes.
- **Speed** on real consoles: see [Performance](performance.md#game-boy-advance).

### Not supported

- Cartridges over 32 MB, such as the 64 MB Game Boy Advance Video cartridges (Shrek, Shrek 2, Shark Tale). BreadBox says why when you try to start one.
- ROMs patched for flash carts (for example SuperCard). They are not shown in the Collection; the original dumps work.
- Gyro and tilt sensors, rumble, the link cable and the e-Reader. WarioWare: Twisted! starts, but its minigames need the gyro.

### Known problems

- **Activision Anthology**: the menus work; entering a game shows a link-cable message and stops.
- **Game Boy Advance Video - Disney Channel Collection - Volume 2** and **Cartoon Network Collection - Special Edition** (Rev 5 dumps): white, frozen picture, as in other emulators. BreadBox keeps running, and the pause menu works.
- **ChuChu Rocket!** reached a link-cable notice in one run where mGBA reached a puzzle; not confirmed.

### Start-up checks (301 cartridges)

- 288 reached their title screen or play with scripted START and A presses.
- 2 that stopped in that run were fixed for 0.9.0 and checked on a real Old 3DS: Ms. Pac-Man - Maze Madness, and Disney Channel Collection Volume 1 (when the cart's own rewind was used).
- The rest are listed above.

| Game | Result |
|---|---|
| 4 Games on One Game Pak (Nicktoons) | Starts |
| Activision Anthology | Problem (see above) |
| Advance Guardian Heroes | Starts |
| Advance Wars | Starts |
| Advance Wars 2 - Black Hole Rising | Starts |
| Alien Hominid | Starts |
| Astro Boy - Omega Factor | Starts |
| Avatar - The Last Airbender | Starts |
| Avatar - The Last Airbender - The Burning Earth | Starts |
| Boktai - The Sun Is in Your Hand | Starts |
| Boktai 2 - Solar Boy Django | Starts |
| Zoku Bokura no Taiyou - Taiyou Shounen Django | Starts |
| Bookworm | Starts |
| Boulder Dash EX | Starts |
| Breath of Fire | Starts |
| Breath of Fire II | Starts |
| Bruce Lee - Return of the Legend | Starts |
| CIMA - The Enemy | Starts |
| CT Special Forces | Starts |
| CT Special Forces 2 - Back in the Trenches | Starts |
| Car Battler Joe | Starts |
| Castlevania - Aria of Sorrow | Starts |
| Castlevania - Circle of the Moon | Starts |
| Castlevania - Harmony of Dissonance | Starts |
| ChuChu Rocket! | Starts |
| Classic NES Series - Bomberman | Starts |
| Classic NES Series - Castlevania | Starts |
| Classic NES Series - Donkey Kong | Starts |
| Classic NES Series - Dr. Mario | Starts |
| Classic NES Series - Excitebike | Starts |
| Classic NES Series - Ice Climber | Starts |
| Classic NES Series - Metroid | Starts |
| Classic NES Series - Pac-Man | Starts |
| Classic NES Series - Super Mario Bros. | Starts |
| Classic NES Series - The Legend of Zelda | Starts |
| Classic NES Series - Xevious | Starts |
| Classic NES Series - Zelda II - The Adventure of Link | Starts |
| Codename - Kids Next Door - Operation S.O.D.A. | Starts |
| Crash Bandicoot - The Huge Adventure | Starts |
| Crash Bandicoot 2 - N-Tranced | Starts |
| Crash Bandicoot Purple - Ripto's Rampage | Starts |
| Crouching Tiger, Hidden Dragon | Starts |
| Donkey Kong 2 | Starts |
| Donkey Kong Country | Starts |
| Donkey Kong Country 2 | Starts |
| Donkey Kong Country 3 | Starts |
| Doom 2 | Starts |
| Doom | Starts |
| Dragon Ball - Advanced Adventure | Starts |
| Dragon Ball GT - Transformation | Starts |
| Dragon Ball Z - Buu's Fury | Starts |
| Dragon Ball Z - Collectible Card Game | Starts |
| Dragon Ball Z - Supersonic Warriors | Starts |
| Dragon Ball Z - Taiketsu | Starts |
| Dragon Ball Z - The Legacy of Goku | Starts |
| Dragon Ball Z - The Legacy of Goku II | Starts |
| Dragon Quest Monsters - Caravan Heart | Starts |
| Drill Dozer | Starts |
| Driver 2 Advance | Starts |
| F-Zero - GP Legend | Starts |
| F-Zero - Maximum Velocity | Starts |
| Final Fantasy 6 Advance # GBA | Starts |
| Final Fantasy I & II - Dawn of Souls | Starts |
| Final Fantasy IV Advance | Starts |
| Final Fantasy Tactics Advance | Starts |
| Final Fantasy V Advance | Starts |
| Finding Nemo | Starts |
| Finding Nemo - The Continuing Adventures | Starts |
| Fire Emblem | Starts |
| Fire Emblem - The Sacred Stones | Starts |
| Fire Pro Wrestling | Starts |
| Fire Pro Wrestling 2 | Starts |
| Fortress | Starts |
| Frogger Advance - The Great Quest | Starts |
| Frogger's Adventures - Temple of the Frog (2) | Starts |
| Frogger's Adventures 2 - The Lost Wand | Starts |
| Ghost Rider | Starts |
| Golden Sun | Starts |
| Golden Sun - The Lost Age | Starts |
| Gradius Advance | Starts |
| Gradius Galaxies | Starts |
| Grand Theft Auto Advance | Starts |
| Gunstar Super Heroes | Starts |
| Guru Logic Champ | Starts |
| Hamtaro - Ham-Ham Games | Starts |
| Hamtaro - Ham-Ham Heartbreak | Starts |
| Harvest Moon - Friends of Mineral Town | Starts |
| Harvest Moon - More Friends of Mineral Town | Starts |
| International Superstar Soccer | Starts |
| Invincible Iron Man, The | Starts |
| Iridion 3D | Starts |
| Iridion II | Starts |
| Jackie Chan Adventures - Legend of the Dark Hand | Starts |
| Karnaaj Rally | Starts |
| Kingdom Hearts - Chain of Memories | Starts |
| Kirby & The Amazing Mirror | Starts |
| Kirby - Nightmare in Dream Land | Starts |
| Klonoa - Empire of Dreams | Starts |
| Klonoa 2 - Dream Champ Tournament | Starts |
| Kururin Paradise | Starts |
| LEGO Bionicle | Starts |
| LEGO Island 2 - The Brickster's Revenge | Starts |
| LEGO Racers 2 | Starts |
| LEGO Star Wars - The Video Game | Starts |
| LEGO Star Wars II - The Original Trilogy | Starts |
| Lady Sia | Starts |
| Legend of Zelda, The - A Link To The Past Four Swords | Starts |
| Legend of Zelda, The - The Minish Cap | Starts |
| Lilo & Stitch | Starts |
| Lilo & Stitch 2 - Haemsterviel Havoc | Starts |
| Lufia - The Ruins of Lore | Starts |
| Lunar Legend | Starts |
| Mario & Luigi - Superstar Saga | Starts |
| Mario Golf - Advance Tour | Starts |
| Mario Kart - Super Circuit | Starts |
| Mario Tennis - Power Tour | Starts |
| Mario vs. Donkey Kong | Starts |
| Super Mario Advance | Starts |
| Super Mario Advance 2 - Super Mario World | Starts |
| Super Mario Advance 3 - Yoshi's Island (Virtual Console) | Starts |
| Super Mario Advance 3 - Yoshi's Island | Starts |
| Super Mario Advance 4 - Super Mario Bros. 3 (V1.1) | Starts |
| Super Mario Advance 4 - Super Mario Bros. 3 | Starts |
| Max Payne | Starts |
| Mazes of Fate | Starts |
| Medabots - Metabee | Starts |
| Medabots - Rokusho | Starts |
| Medal of Honor - Infiltrator | Starts |
| Medal of Honor - Underground | Starts |
| Megaman & Bass | Starts |
| Megaman - Battle Chip Challenge | Starts |
| Megaman - Battle Network | Starts |
| Megaman - Battle Network 2 | Starts |
| Megaman - Battle Network 3 - Blue Version | Starts |
| Megaman - Battle Network 3 - White Version | Starts |
| Megaman - Battle Network 4 - Blue Moon | Starts |
| Megaman - Battle Network 4 - Red Sun | Starts |
| Megaman - Battle Network 5 - Team Colonel | Starts |
| Megaman - Battle Network 5 - Team Protoman | Starts |
| Megaman - Battle Network 6 - Cybeast Falzar | Starts |
| Megaman - Battle Network 6 - Cybeast Gregar | Starts |
| Megaman Zero | Starts |
| Megaman Zero 2 | Starts |
| Megaman Zero 3 | Starts |
| Megaman Zero 4 | Starts |
| Metal Slug Advance | Starts |
| Metroid - Zero Mission | Starts |
| Metroid Fusion | Starts |
| Monster House | Starts |
| Monsters, Inc. | Starts |
| Mortal Kombat Advance | Starts |
| Mother 1+2 | Starts |
| Mother 3 | Starts |
| Mother 3 (Tr) | Starts |
| Mr. Driller 2 | Starts |
| Ms. Pac-Man - Maze Madness | Starts (fixed in 0.9.0; checked on Old 3DS) |
| Naruto - Ninja Council 2 | Starts |
| Need for Speed - Underground 2 (M4) | Starts |
| Ninja Five-0 | Starts |
| One Piece | Starts |
| Phantasy Star Collection | Starts |
| Pitfall - The Lost Expedition | Starts |
| Planet Monsters | Starts |
| Pokemon - Emerald Version | Starts |
| Pokemon - FireRed Version | Starts |
| Pokemon - LeafGreen Version | Starts |
| Pokemon - Ruby Version | Starts |
| Pokemon - Sapphire Version | Starts |
| Pokemon Mystery Dungeon - Red Rescue Team | Starts |
| Pokemon Pinball - Ruby & Sapphire | Starts |
| Pokemon - Adventure Red Chapter | Does not start (over 32 MB) |
| Pokemon - Noon Version (11-11-23) | Starts |
| Power Pro Kun Pocket 1, 2 | Starts |
| Power Pro Kun Pocket 3 | Starts |
| Power Pro Kun Pocket 4 | Starts |
| Power Pro Kun Pocket 5 | Starts |
| Power Pro Kun Pocket 6 | Starts |
| Power Pro Kun Pocket 7 | Starts |
| Prince of Persia - The Sands of Time | Starts |
| Puyo Pop | Starts |
| Puyo Pop Fever | Starts |
| Racing Gears Advance | Starts |
| Rebelstar - Tactical Command | Starts |
| Rescue Heroes - Billy Blazes! | Starts |
| Rhythm Tengoku | Starts |
| Riviera - The Promised Land | Starts |
| Rock 'N Roll Racing | Starts |
| Scurge - Hive | Starts |
| Shaman King - Legacy of the Spirits - Soaring Hawk | Starts |
| Shaman King - Legacy of the Spirits - Sprinting Wolf | Starts |
| Shaman King - Master of Spirits | Starts |
| Shaman King - Master of Spirits 2 | Starts |
| Shin Bokura no Taiyou - Gyakushuu no Sabata | Starts |
| Shining Force - Resurrection of the Dark Dragon | Starts |
| Shining Soul | Starts |
| Shining Soul II | Starts |
| Shrek - Hassle at the Castle | Starts |
| Shrek - Reekin' Havoc | Starts |
| Shrek - Smash n' Crash Racing | Starts |
| Shrek - Super Slam | Starts |
| Shrek - Swamp Kart Speedway | Starts |
| Shrek 2 | Starts |
| Shrek 2 - Beg for Mercy | Starts |
| Shrek the Third | Starts |
| Sigma Star Saga | Starts |
| Sonic 3 - Fighter Sonic | Starts |
| Sonic Advance | Starts |
| Sonic Advance 2 | Starts |
| Sonic Advance 3 (M6) | Starts |
| Sonic Battle | Starts |
| Sonic Pinball Party | Starts |
| Sonic The Hedgehog - Genesis | Starts |
| Spirits & Spells | Starts |
| Legend of Spyro, The - A New Beginning | Starts |
| Legend of Spyro, The - The Eternal Night | Starts |
| Spyro - Attack of the Rhynocs | Starts |
| Spyro - Season of Ice | Starts |
| Spyro 2 - Season of Flame | Starts |
| Spyro Orange - The Cortex Conspiracy | Starts |
| Star Wars - Episode II - Attack of the Clones | Starts |
| Star Wars - Episode III - Revenge of the Sith | Starts |
| Star Wars - Flight of the Falcon | Starts |
| Star Wars - Jedi Power Battles | Starts |
| Star Wars - The New Droid Army | Starts |
| Star Wars Trilogy - Apprentice of the Force | Starts |
| Star X | Starts |
| Street Fighter Alpha 3 | Starts |
| Summon Night - Swordcraft Story | Starts |
| Summon Night - Swordcraft Story 2 | Starts |
| Super Collapse! II | Starts |
| Super Monkey Ball Jr. | Starts |
| Super Robot Taisen - Original Generation | Starts |
| Super Robot Taisen - Original Generation 2 | Starts |
| Super Street Fighter II Turbo - Revival | Starts |
| Sword of Mana | Starts |
| TMNT | Starts |
| Tactics Ogre - The Knight of Lodis | Starts |
| Tales of Phantasia | Starts |
| Tang Tang | Starts |
| Tim Burton's The Nightmare Before Christmas - The Pumpkin King | Starts |
| Tom Clancy's Rainbow Six - Rogue Spear | Starts |
| Lara Croft Tomb Raider - Legend | Starts |
| Lara Croft Tomb Raider - The Prophecy | Starts |
| Turbo Turtle Adventure | Starts |
| Turok - Evolution | Starts |
| Ultimate Spider-Man | Starts |
| V-Rally 3 | Starts |
| Wario Land 4 | Starts |
| WarioWare - Twisted! | Starts |
| WarioWare, Inc. - Mega Microgame$! | Starts |
| WarioWare, Inc. - Minigame Mania | Starts |
| Wolfenstein 3D | Starts |
| Yggdra Union - We'll Never Fight Alone | Starts |
| Yu-Gi-Oh! - 7 Trials to Glory - World Championship Tournament 2005 | Starts |
| Yu-Gi-Oh! - Destiny Board Traveler | Starts |
| Yu-Gi-Oh! - Dungeon Dice Monsters | Starts |
| Yu-Gi-Oh! - GX Duel Academy | Starts |
| Yu-Gi-Oh! - Reshef of Destruction | Starts |
| Yu-Gi-Oh! - The Eternal Duelist Soul | Starts |
| Yu-Gi-Oh! - The Sacred Cards | Starts |
| Yu-Gi-Oh! - Ultimate Masters - World Championship Tournament 2006 | Starts |
| Yu-Gi-Oh! - World Championship Tournament 2004 | Starts |
| Yu-Gi-Oh! - Worldwide Edition - Stairway to the Destined Duel | Starts |
| Game Boy Advance Video - All Grown Up! - Volume 1 | Starts |
| Game Boy Advance Video - Cartoon Network Collection - Limited Edition | Starts |
| Game Boy Advance Video - Cartoon Network Collection - Platinum Edition | Starts |
| Game Boy Advance Video - Cartoon Network Collection - Premium Edition | Starts |
| Game Boy Advance Video - Cartoon Network Collection - Special Edition | Does not play (see above) |
| Game Boy Advance Video - Cartoon Network Collection - Volume 1 | Starts |
| Game Boy Advance Video - Cartoon Network Collection - Volume 2 | Starts |
| Game Boy Advance Video - Codename - Kids Next Door - Volume 1 | Starts |
| Game Boy Advance Video - Disney Channel Collection - Volume 1 | Starts (fixed in 0.9.0; checked on Old 3DS) |
| Game Boy Advance Video - Disney Channel Collection - Volume 2 | Does not play (see above) |
| Game Boy Advance Video - Dora the Explorer - Volume 1 | Starts |
| Game Boy Advance Video - Dragon Ball GT - Volume 1 | Starts |
| Game Boy Advance Video - Nicktoons - Volume 3 | Starts |
| Game Boy Advance Video - Nicktoons Collection - Volume 1 | Starts |
| Game Boy Advance Video - Nicktoons Collection - Volume 2 | Starts |
| Game Boy Advance Video - Pokemon - Volume 1 | Starts |
| Game Boy Advance Video - Pokemon - Volume 2 | Starts |
| Game Boy Advance Video - Pokemon - Volume 3 | Starts |
| Game Boy Advance Video - Pokemon - Volume 4 | Starts |
| Game Boy Advance Video - Shark Tale | Does not start (over 32 MB) |
| Game Boy Advance Video - Shrek | Does not start (over 32 MB) |
| Game Boy Advance Video - Shrek 2 | Does not start (over 32 MB) |
| Game Boy Advance Video - Sonic X - Volume 1 | Starts |
| Game Boy Advance Video - SpongeBob SquarePants - Volume 1 | Starts |
| Game Boy Advance Video - SpongeBob SquarePants - Volume 2 | Starts |
| Game Boy Advance Video - SpongeBob SquarePants - Volume 3 | Starts |
| Game Boy Advance Video - Strawberry Shortcake - Volume 1 | Starts |
| Game Boy Advance Video - Super Robot Monkey Team - Hyper Force Go! - Volume 1 | Starts |
| Game Boy Advance Video - Teenage Mutant Ninja Turtles - Things Change | Starts |
| Game Boy Advance Video - The Adventures of Jimmy Neutron Boy Genius - Volume 1 | Starts |
| Game Boy Advance Video - The Fairly OddParents! - Volume 1 | Starts |
| Game Boy Advance Video - The Fairly OddParents! - Volume 2 | Starts |
| Game Boy Advance Video - The Proud Family - Volume 1 | Starts |
| Game Boy Advance Video - Yu-Gi-Oh! - Yugi vs. Joey | Starts |

## Tell us what you find

Open an issue with your console, the system, the game, its region and `log.txt`
(see [CONTRIBUTING](../CONTRIBUTING.md)). The **Game report** form is the quickest way.
