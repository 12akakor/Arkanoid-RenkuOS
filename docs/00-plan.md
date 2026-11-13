# Arkanoid (MSX) as a native RenkuOS application

Technical plan, 13 November 2026.

Source: the manually annotated disassembly of Taito's MSX Arkanoid by
tiburoncio, <https://github.com/mcolom/arkanoid_msx_disasm> (9,289 lines in
`disassembly.asm` plus 27 include files, 15,864 lines of assembly in total).
Target: a native C++ application for RenkuOS (a fork of Haiku, same Be API),
using the Be API directly and following the Haiku coding guidelines.

This document is the plan. No game code has been written yet; it waits for
the go-ahead.


## 1. What the original program is

### 1.1 Hardware

An MSX1 program in a 32 KB cartridge (`org 0x4000`). It is not a MegaROM:
there is no bank switching, no disk and no cassette code. The disassembly
compiles back to the original ROM byte for byte, which is the ground truth
the whole port is measured against:

    z80asm disassembly.asm && shasum a.bin
    2183f07fa3ba87360100b2fa21fda0f55c0f8814  a.bin

| Part | What it is | What the port needs |
|---|---|---|
| CPU | Z80 at 3.58 MHz | nothing (logic is rewritten in C++) |
| Video | TMS9918A, SCREEN 2 (Graphics II): 256x192, 32x24 cells of 8x8, 2 colours per cell, 15 fixed colours plus transparent | a small rasteriser of our own |
| Sprites | 32 sprites, 16x16, one colour each, 4 per line | free of the 4-per-line limit |
| Sound | AY-3-8910 (PSG) through the BIOS `WRTPSG`, 3 tone voices, noise, envelope | a PSG model and a bit-exact driver |
| Input | keyboard through `SNSMAT`, joystick, and the MSX analogue paddle read serially through PSG port 14/15 | Be API input |
| Timing | **all** game logic runs in the 60 Hz VBlank hook installed at `0xFD9A`; the main loop only does `halt` and the pause handling | a fixed 60 Hz tick |

VRAM layout, established at start-up from `VDP_BASE_POINTERS` (0x4376):

    name table            0x1800   (32x24, 1 byte per cell)
    colour table          0x2000   (8 bytes per pattern, one per pixel row)
    pattern table         0x0000   (256 patterns of 8 bytes)
    sprite attributes     0x1B00   (32 x 4 bytes: Y, X, pattern, colour)
    sprite patterns       0x3800   (16x16 patterns, 64 bytes each)

### 1.2 Program flow

    ROM_START (0x402A)
      slot switching, clear RAM 0xE000-0xE5B3, VDP registers, INIGRP,
      load TITLE_TILES + TITLE_COLORS, load sprite definitions,
      FILL_BRICK_ACTION_TABLE (unrolls the per-level brick actions),
      install VDP_HOOK_HANDLER at 0xFD9A, ei
    forever:
      halt                            <- waits for VBlank
      pause handling (bit 6 of CONTROLS), then "PAUSE" on screen
    VDP_HOOK_HANDLER (0x424A)         <- one tick, 60 times per second
      SOUND_ISR_UPDATE
      read keyboard matrix (SNSMAT rows 6, 7, 8) into KEYBOARD_INPUT,
        derive CONTROLS from the edges (new presses) and run the two cheats
      read the analogue paddle (PSG port 14/15), PADDLE_COUNT / PADDLE_STATUS
      hide all 32 sprites, copy SPR_PARAMS to SPRITE_ATTRIBS_AREA,
        swap sprites 0 and 4 when a capsule falls (the MSX 4-per-line trick)
      write the 128 sprite attribute bytes to VRAM
      dispatch on GAME_TRANSITION_ACTION (0xE00A):
        0 START_LEVEL  -> ENASCR + DRAW_TITLE_SCREEN (title, story, demo, start)
        1 PLAY_LEVEL   -> EXECUTE_VAUS_ACTION_AND_LASERS_STEP_AND_PORTAL_ANIMATION
                          UPDATE_OBJECTS (capsules, ball movement, hard bricks,
                          alien vertical direction)
                          CHECK_DEMO_TIMEOUT (aliens, lasers vs aliens,
                          alien vs Vaus, doors; or the whole Doh fight)
                          DRAW_SCORE_NUMBERS
        2 NEXT_LEVEL   -> NEXT_OR_SAME_LEVEL (next level / life lost / ending)
      play the first queued sound from SOUNDS_BUFFER and shift the queue

Every frame is one tick. There is no variable timestep anywhere, no frame
skipping in the logic, and `DELAY_HL_TICKS` simply halts for N frames.

### 1.3 The game

- **Levels**: 32 (0-31) plus level 32, the Doh fight; finishing 32 shows the
  ending and returns to the title (`FINAL_LEVEL` = 32). Level 0 is the first.
- **Bricks**: 12 columns x 11 rows in a 17-byte bitmask (`BRICK_MAP`, 136 bits
  for 132 cells), 2x2 characters each. Colour per brick comes from the level
  colour table; the brick *behaviour* per level comes from a nibble RLE table
  (`compressed_brick_actions_per_level.asm`) that the original unrolls at
  start-up into 32 tables of 11x11 bytes, one of five actions each:
  plain hit, hit + capsule, hard brick, unbreakable, reset unused.
- **Hard bricks** need `LEVEL/8 + 2` hits, not always 2.
- **Scoring** is BCD (`SCORE_BCD`, `HIGH_SCORE_BCD`), extra life at 20000.
- **Balls**: three, 20-byte entries (`BALL_TABLE1..3`), 8-bit pixel positions,
  three actions (initialise, follow glued Vaus, move). Speed is not a velocity
  but a position in a 16-entry table; each entry gives "add delta N times"
  (`SPEED_MULTIPLIER`, `MOVE_TARGET`), so a ball moves 1-3 pixels per tick.
  Skewness is one of 8 directions (3 = straight up), each with two speed
  variants. `BALL_SPEED_TABLE` (1,1,...,1,2,4,8,16,24,31) makes the ball
  accelerate in steps; every 40 border bounces the skewness changes.
- **Collision** (`CHECK_BRICK_HIT_AND_BOUNCE_BALL`, 2,263 lines of assembly
  including its comments) is a swept test of the ball's *leading contact
  point*, not its centre: moving up Y-24, down Y-19, left X-17, right X-12.
  It compares the brick cell of the contact point now and one velocity step
  ago, classifies the crossing (same cell, vertical face, horizontal face,
  corner, wall-adjacent) separately for each of the four directions, and uses
  `TICKS_TO_HIT` as a sub-step counter to resolve how far along the auxiliary
  slope the boundary was crossed. It handles double impacts in one tick.
  This is the one routine that must be understood line by line rather than
  paraphrased.
- **Vaus** is a 10-byte state machine (`VAUS_TABLE`): wait/ready, keep,
  enlarging, shrinking, laser, un-laser, exploding, entering the portal, with
  a resizing step, a 10-step lasering step and two destruction counters.
- **Capsules**: 8 types by colour, each with an action (slow ball, sticky,
  enlarge, three balls, lasers, ...), a falling sprite that the MSX has to
  flicker by swapping sprite slots 0 and 4.
- **Aliens and doors**: an alien table of 20-byte entries, three types, one to
  three aliens per level (`TABLE_ALIENS_PER_LEVEL` starts 1, 2, 3 and is 3
  after that). Aliens leave through the left or right door, always the
  one farther from Vaus, do a circular walk of scripted steps, can be shot by
  lasers, hit by Vaus or by a ball, and can travel through bricks.
- **Doh**: 8 columns x 12 rows of characters drawn from a row table, a hit
  table with per-cycle colours, `DOH_RECEIVED_HITS`, up to 4 bullets that
  follow Vaus with a skewness that changes, and a defeat animation that
  removes one row at a time.
- **Presentation**: title screen (logo, "PUSH START BUTTON", Taito strings),
  a 9-line x 26-character story that is typed out at 2 ticks per character,
  a demo that starts after 480 idle ticks on the title and levels 12, 3, 6, 1
  in turn, with a 2880-tick timeout; "ROUND n", the frame around the
  playfield, scores in ASCII decoded from BCD, "GAME OVER" drawn with
  sprites, and the ending text animation.
- **Two documented cheats** (Up+Down+Graph x4 for 240 lives, Left+Right+Graph
  x4 to continue from the last level). They come along with the port for
  free, because they live in the input edge detection.

### 1.4 Sound

The PSG player is `sound_src.asm` (1,405 lines): `PLAY_SOUND` and
`SOUND_ISR_UPDATE`, 28 five-byte descriptors (voice mask, period mask,
volume mask, sequence pointer), two concurrent streams with priority, chaining
and repeat, and sequence data. Sound codes 0-12 are effects, 192-200 are
musical pieces (`SOUND_VAUS_INCREASES_SIZE_H` .. `SOUND_DOH_LEVEL_STARTS`).
It writes the PSG registers directly through `WRTPSG`.


## 2. Architecture

The rule is the one this project already established with Metal Gear: **no
emulation in the game**. No Z80, no imitated VDP, no BIOS. Every routine is
rewritten in C++, and the original is kept beside it as a test oracle.

The game core does not depend on RenkuOS, so it builds and runs on any system
for testing. Exactly one file, the platform layer, knows about the Be API.

    arkanoid/
      Makefile                    make / make package / make tools
      docs/00-plan.md             this document
      docs/01-progress.md         what is done, with test numbers
      src/
        main.cpp                  calls GameMain(), nothing else
        game/                     portable core (stdint types, no RenkuOS)
          State.h                 the whole original RAM as one struct
          Screen.h/.cpp           VDP state: pattern, colour, name, sprites
          Renderer.h/.cpp         256x192 indexed frame out of Screen
          Data.h/.cpp             graphics tables (title, in-game, sprites)
          DataTables.cpp          the same tables as C++ arrays, generated
          DataEmbedded.h          (byte-identical to the ROM, see 4/Phase 0)
          Levels.h/.cpp           32 bitmaps, colour tables, brick actions
          Game.h/.cpp             tick, transition state machine, title, story
          Ball.h/.cpp             ball table, movement, skewness, speed
          Vaus.h/.cpp             Vaus state machine, resizing, lasers, portal
          Bricks.h/.cpp           brick map, hard bricks, hit effects, repaint
          Collision.h/.cpp        the swept brick collision
          Capsules.h/.cpp
          Aliens.h/.cpp           aliens, doors, alien-vs-everything
          Doh.h/.cpp              the final boss
          Scores.h/.cpp           BCD scoring, ASCII score drawing
          Text.h/.cpp             title, story, round, game over, ending
          Sound.h/.cpp            the PSG driver state machine
        audio/                    portable
          PsgSound.h/.cpp         AY-3-8910 driven by register writes at
                                  their CPU cycle (Oh Shit! port)
          GameAudio.h/.cpp        feeds the driver's writes to PsgSound
          MusicRenderer.h/.cpp    optional, Phase 5: SoundFont instruments
        platform/                 RenkuOS only
          Platform.h              the interface the core uses
          Keys.h
          PlatformRenku.cpp       Be API back end
          VideoScaler.h/.cpp      nearest / sharp / xBR scaling
        ref/                      test oracle only, not part of the game
          Z80.h/.cpp              Z80 interpreter (from Metal Gear)
          Tms9918.h/.cpp          MSX1 VDP (from the Oh Shit! port; its
                                  Graphic 2 path is new ground, see 3.3)
          Psg.h/.cpp              PSG model (from the Oh Shit! port)
          Machine1.h/.cpp         MSX1 slots, PPI keyboard, TMS9918A, PSG,
                                  real C-BIOS as the BIOS (Oh Shit! port)
      tests/
        routine_diff.cpp          one routine at a time, original vs port
        game_diff.cpp             whole game, frame by frame, scripted input
        gfx_diff.cpp              every screen, original VRAM vs port bitmap
        sound_diff.cpp            every sound, register by register
      tools/
        romcheck.cpp              identify/verify the ROM
        shot.cpp                  headless: run N ticks, write PNG
        export_gfx.cpp            dump tiles/sprites for the HD set later
        check/renku_syntax.sh     compile-check the RenkuOS code against real headers

### 2.1 The state struct

The original keeps everything in RAM from 0xE000. That becomes one struct,
field for field, with the original names in a comment and the original
address as the field comment:

    struct GameState {
        uint8_t  demoLevel;          // DEMO_LEVEL            0xE000
        uint8_t  cheat1Activated;    // CHEAT1_ACTIVATED      0xE001
        ...
        uint8_t  brickMap[17];       // BRICK_MAP             0xE027
        ...
    };

This is what makes differential testing possible: dump 0xE000-0xE5FF from
the oracle and from the port and compare 1,536 bytes, not a hand-written
summary of the interesting ones. It also means tables that the original
"patches" at run time (the unrolled brick actions, the decompressed colour
tables) are patched identically by the port.

### 2.2 Rendering

`Screen` is the VRAM (16 KB, laid out exactly as above) plus the VDP
registers. `Renderer` turns it into a 256x192 8-bit indexed frame: for each
of the 32x24 cells it takes the pattern from 0x0000, the foreground and
background colour of each of the 8 pixel rows from 0x2000, emits 8 pixels,
then draws the 32 sprites 16x16 in order with colour 0 transparent.

Because that is a faithful TMS9918A, the picture is *exactly* the original
one, and the platform layer is free to scale it (nearest, sharp, xBR) or
later to replace cells with HD artwork, without the game noticing.

The 15 TMS9918A colours are fixed:

    transparent, black, medium green, light green, dark blue, light blue,
    dark red, cyan, medium red, light red, dark yellow, light yellow,
    dark green, magenta, gray, white

### 2.3 Platform interface

Same shape as the Metal Gear port, because it is already proven on RenkuOS:

    bool PlatInit(int width, int height, bool fullscreen, const char* title);
    void PlatPresent(const uint8_t* indexedFrame);   // 256x192
    void PlatSetPalette(const uint8_t* rgb);         // 16 x 3, set once
    void PlatSetScaler(const char* name);
    bool PlatSetFullscreen(bool fullscreen);
    void PlatToggleStats(void);
    void PlatSaveScreenshot(void);
    void PlatShowMessage(const char* text);
    void PlatAlert(const char* title, const char* text);
    // input
    uint32_t PlatKeys(void);            // bit mask, edge-detected by the core
    bool     PlatPaddle(int* position); // analogue 0..255, false if none
    // settings
    BPath    PlatSettingsPath(const char* name);

The game runs on its own thread (`GameMain`); the `BApplication` message loop
stays on the main thread. That is the only threading model that does not
fight the Be API.

### 2.4 Timing

One tick per 1/60 s, driven by `system_time()` (microseconds) with a
catch-up loop, never by the message loop. On the original a tick that took
too long simply delayed the next one; here the core may run two ticks in one
frame if the machine hiccups, but the logic always sees exactly 60 Hz. If
the display refresh cannot be matched, we present the newest frame and count
the dropped ones (shown with the stats overlay), exactly like Metal Gear.


### 2.5 What is reused from the Oh Shit! port

Oh Shit! (<https://github.com/12akakor/OhShit-Haiku>) is the same problem one
machine step earlier: an MSX1 game, rewritten in C++ and checked the same
way. It is the same hardware class as Arkanoid, so most of the infrastructure
is not written again:

| Piece | From | State | What Arkanoid needs on top |
|---|---|---|---|
| `src/z80` | Metal Gear | used by 3 ports | nothing |
| `src/video/Tms9918` | Oh Shit! | Graphic 1 proven against openMSX | Graphic 2 (SCREEN 2) and its colour table addressing exercised for the first time; 16x16 sprites; the 4-per-line limit and `SetSpriteLimit(false)` already there |
| `src/msx1/Psg` | Metal Gear | used by 2 ports | nothing |
| `src/msx1/Machine1` | Oh Shit! | matches openMSX on VRAM and RAM | a 32 KB cartridge in slot 1 that takes over page 2 by itself (`ENASLT`), and the BIOS entries in the table below |
| `src/audio/PsgSound` | Oh Shit! | cycle-timed, stereo, DC filter | nothing: Arkanoid writes the same registers, just without the 1-bit speech |
| `src/platform` | Metal Gear, extended for Oh Shit! | syntax-checked, **never run on RenkuOS** | a settings list for this game; see the risk table |
| `tools/check/renku_syntax.sh` | Oh Shit! | in use | point it at `src/platform/PlatformRenku.cpp` and `src/main.cpp` |
| `tools/osrun` | Oh Shit! | in use | screenshots and WAV for Arkanoid's oracle |
| `tools/dis` | Oh Shit! | separates code from data | not needed: the disassembly already exists |
| `Makefile` pattern | both | in use | new lists, same shape |

The BIOS pieces Arkanoid uses are not the ones Oh Shit! uses. It needs
`RSLREG`, `ENASLT`, `SNSMAT`, `WRTPSG`, `RDPSG`, `FILVRM`, `LDIRVM`,
`WRTVRM`, `RDVDP`, `SETWRT`, `CHGMOD`, `INIGRP`, `DISSCR`, `ENASCR`,
`CHGCLR` and `CHGCAP`; the reusable method is Oh Shit!'s — rewrite each one
with the effect C-BIOS has on what the game can see — and five of them
(`FILVRM`, `LDIRVM`, `WRTVRM`, `WRTPSG`, `SNSMAT`) already exist there.

Two consequences for the plan: Phase 0 shrinks to verification and data
extraction, and the sound engine arrives already written, so Phase 3 becomes
the driver instead of the chip.


## 3. Verification

This is the part that decides whether the thing is a port or an
approximation, so it comes before the port, not after it.

### 3.1 The oracle

`src/ref` is a minimal MSX1: the Z80 interpreter from the Metal Gear port
(same licence, same author, already used to verify 17,000 instructions), the
32 KB ROM at 0x4000-0xBFFF, 16 KB of VRAM, the PSG registers, and traps for
the BIOS entry points the game calls:

    RSLREG ENASLT SNSMAT WRTPSG RDPSG FILVRM LDIRVM WRTVRM RDVDP
    SETWRT CHGMOD INIGRP DISSCR ENASCR CHGCLR CHGCAP

plus the interrupt: `HALT` ends a tick, the VBlank hook at 0xFD9A is called,
tick counter increments. Metal Gear's VDP only does SCREEN 5; this one needs
the SCREEN 2 rasteriser, the sprite limit and the VDP status register, which
is less work than what is there already.

Feeding it the ROM gives us, for any tick and any input script, the exact
RAM, VRAM and PSG register dump of the original. That is the target.

### 3.2 The tests

| Test | Runs | Compares | Pass criterion |
|---|---|---|---|
| `routine_diff` | one C++ routine, from a saved oracle state, against the same Z80 routine in the running oracle | all of RAM, VRAM and PSG after the call; also the return value and flags | 0 differences |
| `game_diff` | the whole game from a scripted input trace: title, start, one level per level, capsules, aliens, Doh, ending, a lost life, game over, both cheats | RAM + VRAM + PSG every tick | 0 differences, tick by tick |
| `gfx_diff` | every distinct screen (title, 32 levels, Doh, story, ending, frame) | the rendered 256x192 bitmap against the bitmap rendered from oracle VRAM | 0 differing pixels |
| `sound_diff` | every effect and every music, in combination | PSG registers every tick | 0 differences |

`routine_diff` is the workhorse: it is what lets a 2,263-line collision
routine be ported with confidence, one direction case at a time. Each case
of the big routine gets its own entry in the registry, with a state captured
from the original that exercises exactly that case (corner crossing, double
impact, wall-adjacent, hard brick, unbreakable brick, ball lost).

`game_diff` is the acceptance test: a complete play session must produce the
same game state as the original, frame for frame.

**What the pixel comparison can and cannot prove.** The port draws through
its own `Renderer`, and the oracle draws through `src/ref/Tms9918`, so
`gfx_diff` compares two independent rasterisers and a disagreement is a real
finding. What it cannot catch is a misreading of the TMS9918 datasheet that
both share. That is covered separately: the palette, the Graphic 2 pattern
and colour table addressing and the sprite layout are checked against the
datasheet and against the original's own screens (the title logo has to be
legible, the frame has to land on the cells the code writes, the bricks have
to sit where the level bitmaps put them). The VRAM and register diff proves
the *state*; the pixel diff checks the drawing, not the hardware.

### 3.3 What I can and cannot check here

I work on a Linux machine with g++ 13, no RenkuOS and no RenkuOS headers. So:

- **Can, here**: build and run the core, the oracle, all four tests, the
  headless renderer, and produce PNG screenshots of every screen to look at.
- **Can, here**: syntax-check the Be API code against the real RenkuOS
  headers with `tools/check/renku_syntax.sh`, taken from the Oh Shit! port.
  It fetches the headers read-only and compiles `PlatformRenku.cpp` and
  `main.cpp` with `-fsyntax-only`. That catches typos, wrong signatures and
  missing includes, and it is not a substitute for linking. Point it at
  RenkuOS's source tree instead of Haiku's when the two diverge.
- **Cannot, here**: link or run the RenkuOS binary. That needs `make` on your
  RenkuOS machine (or a RenkuOS VM). I will keep the RenkuOS-specific
  surface to one file so this check is quick for you, and I will say plainly
  which parts are unbuilt.


## 4. Phases

Each phase ends with something runnable and a test number, not with "done".

### Phase 0 - Foundation (no game logic yet)

1. Build `z80asm` from source into `~/.local` (no root here), assemble
   `disassembly.asm`, verify the SHA1 above. If it does not match, stop and
   tell you: the port would be measured against the wrong code.
2. Install C-BIOS without root (the tests need it, as in the Oh Shit! port).
3. Bring in `src/z80`, `src/video/Tms9918`, `src/msx1/Psg`,
   `src/msx1/Machine1` and `tools/osrun` from the Oh Shit! port, then make
   Machine1 an Arkanoid machine: 32 KB cartridge in slot 1 switching page 2
   to itself, plus the BIOS entries of section 2.5. Test: boot to the title
   screen, dump VRAM and the 0xE000 game variables, render to PNG.
4. Verify the Graphic 2 path against the datasheet and against the game's
   own screens, since no other port has exercised it yet.
5. Data extraction: the pattern, colour, sprite, level and level-colour
   tables, addressed by ROM offset, exactly as the .asm blocks document them.
6. `tools/embed_data`: generates `src/game/DataTables.cpp` from those ROM
   offsets, and a test that the generated arrays equal the ROM bytes exactly.
   The game therefore runs with no ROM present, and with one it runs on the
   same bytes; a mismatch fails the build instead of shipping a wrong table.

Deliverable: a PNG of the original title screen, produced by code, plus the
first four numbers (SHA1, tick count, VRAM size, screen checksum).

### Phase 1 - Core and one playable level

`State`, `Screen`, `Renderer`, `Levels`, `Bricks` (bitmap, hard bricks,
repaint), `Ball` (movement, skewness, speed, three balls), `Collision`,
`Vaus` (movement, enlarge, shrink, lose a life), `Scores`, `Text` (round
message, frame, HUD), `Game` (tick, transition state machine, title).

Already here: keyboard input through `Platform`, so the game is playable
headless with a scripted or real keyboard, and `routine_diff` entries for
every routine above.

Deliverable: play level 1 and lose a life, with `game_diff` clean for those
ticks.

### Phase 2 - The rest of the game

Capsules (all 8), lasers, portal, aliens, doors, story text, demo and its
timeout, Doh (all of it), ending, game over, both cheats. Level 32 and the
ending are where most of the remaining unknowns are.

Deliverable: `game_diff` clean over a full session from level 0 to the ending.

### Phase 3 - Sound

Port `PLAY_SOUND` and `SOUND_ISR_UPDATE` bit-exactly (two streams, priority,
chain, repeat, the 28 descriptors), with `sound_diff` as the check. The chip
itself is `PsgSound` from the Oh Shit! port, which takes every register write
at its CPU cycle and already produces stereo at 44.1 kHz with a DC filter;
the driver's writes are fed to it directly. The "original sound" mode is
this one, and it stays the default; the enhanced music of Phase 5 sits on
top of the same note events.

### Phase 4 - RenkuOS

This is mostly a copy: `src/platform` and `src/main.cpp` come from the Oh
Shit! port, with `PlatLoadSettings`/`PlatSaveSettings` already there for the
high score. New work is the settings list for this game, the icon and
`src/Arkanoid.rdef` (`application/x-vnd.Akakor-Arkanoid`), and the package.

`PlatformRenku.cpp`: `BApplication`/`BWindow`/`BView`, an offscreen
`BBitmap(B_RGB32)`, `DrawBitmap` in `Draw()`, `BScreen` for full screen,
`get_key_info()` polling, `BJoystick` for gamepads, mouse as an analogue
paddle, `BSoundPlayer` for audio, `find_directory` for settings, `BAlert` for
errors, `BTranslatorRoster` + `BitmapStream` for PNG screenshots,
`BMessage`/`BLooper` for the small options panel. Scaling and full screen
like Metal Gear. Icon and `.rdef`, `Makefile` target `package` producing a
`.hpkg` with a Deskbar entry.

Then the parts only you can do: `make` and `make package` on RenkuOS, and a
first play.

### Phase 5 - Better graphics, better music (separate go-ahead)

Not in this plan's scope, but the architecture is already shaped for it, and
it is exactly what the Metal Gear port does:

- **Graphics**: because `Renderer` draws cells from the VDP state, a cell can
  be identified by a key (hash of its 64 pattern bits plus its 8 colour
  bytes, as `export_gfx` writes them) and replaced by an HD tile painted
  over the scaled picture, sprites on top. xBR as the fallback, own drawings
  taking priority, `F6` settings like Metal Gear. The 32 brick colours and
  the fixed 15-colour palette mean the HD set is small: a few dozen brick
  and frame tiles, 16x16 sprites, one font.
- **Music**: the driver reports every note start; a `MusicRenderer` plays it
  on a General MIDI instrument of a SoundFont with reverb (TinySoundFont,
  MIT, plus GeneralUser GS as in Metal Gear), with the original AY sound kept
  as a switchable mode. Effects keep their original character on the PSG
  synth, which is what makes them sound right.
- **Also from Metal Gear**: the settings window, the timing overlay, the
  smooth transitions, the screenshot key, the same key layout where it makes
  sense.


## 5. The Haiku coding guidelines, which RenkuOS follows

Concretely, not just "we will follow them":

- Tabs for indentation, 4 columns, **no line over 100 columns**, one tab per
  expression level when wrapping, `&&` at the start of the continuation.
- Two blank lines between functions, between include and define blocks, and
  between defines and definitions. Newline at end of every file.
- `ClassName`, `MethodName`, `fMemberVariable`, `kConstant`, `gGlobal`,
  `sStatic`, `_PrivateMethod`.
- `int32`, `uint8`, `status_t`, `off_t`, `bigtime_t` from `SupportDefs.h` in
  the platform layer; the core uses `uint8_t` and friends so the tests build
  on Linux (a documented deviation, as in the Metal Gear port).
- `BString` and its `<<` instead of `sprintf`; `BObjectList` over `BList`;
  `AutoLock` (not `BAutolock`, not `Lock()`/`Unlock()`); stack objects over
  heap; constructor initialiser lists; `NULL`, not `0`, for pointers.
- No `else` after a `return`, no assignment inside `if`, no redundant
  parentheses except around bit tests (`if ((a & 3) != 0)`), boolean
  conditions instead of pointer/integer truth, C++ casts.
- Include order: own header first, then POSIX, then RenkuOS, then local,
  alphabetised inside each group.
- MIT licence header in the exact preferred form, two blank lines after it,
  header guards with two blank lines before `#endif`.

One deliberate exception, and it is a real one: **routine names are RenkuOS
style, not the disassembly's.** `CHECK_BRICK_HIT_AND_BOUNCE_BALL` becomes
`Collision::CheckBrickHitAndBounceBall()`, and every such function carries
the original name and ROM address in its doc comment:

    /*! Checks the ball against the brick field and bounces it.
        Original: CHECK_BRICK_HIT_AND_BOUNCE_BALL at 0x9C2D.

        The contact point is the leading edge of the ball sprite, not its
        centre, so the offsets differ per direction.
    */
    void
    Collision::CheckBrickHitAndBounceBall(Ball& ball)

The differential test registry uses the same mapping, so the link from the
C++ back to a line of Z80 is never lost, and the guidelines are not bent.


## 6. Legal

Arkanoid is (c) Taito. The disassembly says "commercial use is strictly
prohibited". This project is a non-commercial preservation port; that is the
basis on which the data decision below was taken, and it is recorded here so
nobody has to guess later.

- Our own code stays under the MIT licence already in `LICENSE`.
- No ROM image is committed. `docs` and the repository carry no game data
  file, no screenshots of the original and no recordings.
- The graphics, level and sound tables are generated into
  `src/game/DataTables.cpp` by `tools/embed_data`, byte for byte from the
disassembly's own data blocks (decision of 13 November 2026, on the basis
  that the game is not commercially exploited). Regenerating them from the
  disassembly is one command, so the provenance of every byte is checkable.
- Anyone who would rather not distribute that data can build with
  `make DATA=rom` and ship no tables at all: the loader then reads them from
  a ROM the player supplies.


## 7. Risks

| Risk | Why it matters | Mitigation |
|---|---|---|
| The drift test fails on some routine | a port that is "almost" right feels wrong: the ball bounces at the wrong place and it is obvious | `routine_diff` per case, before moving on; no routine is accepted on inspection alone |
| The collision routine resists translation | 2,263 lines, four duplicated direction cases, sub-step refinement | port it case by case with a captured oracle state for each, and keep the structure, not a "cleaner" rewrite |
| Oracle VDP differs from real hardware | sprite priority, 4-per-line, status bits | the reused model already matched openMSX on VRAM and RAM for Oh Shit!; the parts Arkanoid adds (Graphic 2, 16x16 sprites) are checked before they are trusted |
| I cannot run the RenkuOS build | the Be API surface is never executed here | one small platform file, syntax-checked against the real headers, plus `make` on your machine |
| Scope creep into better graphics/music | the faithful port is the prerequisite for both | Phase 5 needs its own go-ahead |
| The reused platform layer has never run on RenkuOS | Oh Shit! itself says so: it compiles and passes the syntax check, nothing more. A runtime bug here would appear in Arkanoid too and would be blamed on the port | it is one file with a narrow interface; the first run on RenkuOS is an explicit step, and anything I find will be reported as belonging to the shared layer, not to this game |
| The Graphic 2 renderer is used for the first time here | Oh Shit! tested Graphic 1 only | verify against the datasheet and the game's own screens before trusting a single `gfx_diff` number |
| C-BIOS is needed for the tests | it is the oracle's BIOS, and it is not installed on this machine and cannot be installed with root | fetch it into the workspace, as the Oh Shit! port expects (`/usr/share/cbios` or a local path) |


## 8. Decisions taken (13 November 2026)

1. **Game data: both, with the ROM as the authority.** The port reads the
   tables from a ROM at start-up when one is present (built with `z80asm`
   from the disassembly, or dumped from a cartridge), and otherwise uses the
   tables generated into the source, so it runs out of the box. The two are
   proved byte-identical by a test, so there is one behaviour, not two. The
   differential oracle always uses the ROM.
2. **Verification: the full differential oracle**, as described in section 3.
   No routine is accepted on inspection alone.
3. **Input: keyboard, gamepad and mouse as the analogue paddle**, selected
   the way the original selects them (whichever device is used at the title
   screen sets `USE_VAUS_PADDLE`), with a settings override.
4. **Comforts: yes.** High score and window size persisted in
   `~/config/settings/Arkanoid`, F11 full screen, F9 screenshot, F8 timing
   overlay, scaler choice, Deskbar entry and a `.hpkg`.

## The paddle - decided 13 November 2026

The paddle is a `BJoystick`, and it is optional. With no joystick present the
game is played from the keyboard, exactly as before, and START (SPACE) at the
title always selects the cursor keys. On the RenkuOS side the joystick's X axis
becomes the paddle position and its first button the paddle's button, both fed
to the ported input stage; nothing in the game's own logic depends on either,
and the keyboard path never goes through them.
