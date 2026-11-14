# Progress of the Arkanoid port

## 13 November 2026: Phase 0, the ROM, the reference machine, the picture

**The ROM is the original, byte for byte.** `tools/build_rom.sh` assembles
`disassembly.asm` with z80asm 1.8 and checks the result:

    build/arkanoid.rom: 32768 bytes, sha1 2183f07fa3ba87360100b2fa21fda0f55c0f8814

That is the checksum the disassembly's README gives for the original
cartridge, so the code the port is measured against is the real one.

One line of the disassembly does not assemble with z80asm 1.8: line 4861,
`ld (ix), 0` (`;7319  dd 36 00 00`). z80asm 1.8 segfaults on the IX/IY form
without a displacement; `ld (ix+0), 0` encodes to the same four bytes the
disassembly's own comment gives. `build_rom.sh` makes that one substitution
in a copy under `build/rom`, so the reference source stays untouched.

**The reference machine is in place.** `src/ref` holds the Z80, the
TMS9918A, the PSG and the MSX1 machine from the Oh Shit! port (Z80 from
Metal Gear), with the real C-BIOS 0.29a as the BIOS
(`third_party/cbios/cbios_main_msx1.rom`, `cbios_logo_msx1.rom`). No BIOS
routine is emulated in C++ for the tests.

**The original runs.** `tools/shot.cpp` boots the cartridge, runs it for a
number of frames, holds keys on request and writes the picture, optionally
with the video memory and the RAM:

    make rom && make shot
    ./shot build/arkanoid.rom 300 build/title.png
    ./shot build/arkanoid.rom 900 build/level1.png -hold 8 0 320 6 -ram build/level1.ram

At 300 frames: PC `0x40D7` (the game's `halt` loop), slot register `0xD4`
(BIOS, cartridge in pages 1 and 2, RAM in page 3), H.TIMI hook at `0xFD9A`
holding `C3 4A 42`, and VDP registers `02 E2 06 FF 03 36 07 F0` - exactly
the five table addresses the game's own `VDP_BASE_POINTERS` lists.

At 900 frames with SPACE held at frame 320, the game is in level 0 with
`GAME_STATE` 1, `LIVES` 2, `BRICKS_LEFT` 66, the ball glued to Vaus at
`0xA9` and the Vaus at `0x50`.

### The paddle

The reference machine reads an idle-high trigger line, the game sees an edge
on its first paddle read and sets `USE_VAUS_PADDLE` (`0xE00C`) to 1: it
switches itself to paddle mode. This is what the original does on real
hardware with no paddle attached, not a fault of the machine. Pressing START
(SPACE) makes the game's own code at `0x42F6` set it back to 0, which is how
a player selects the cursor keys on a real MSX; the scripted run above does
exactly that and the whole game then runs in keyboard mode.

The analogue paddle itself is left for later, as agreed: on RenkuOS the
pointer and `BJoystick` will drive it, and the port needs a paddle model with
a position and a button whether or not the reference machine has one.

### The cartridge as data

`tools/embed_rom.cpp` writes `src/game/RomImage.cpp`: the cartridge byte for
byte, generated only from a ROM that passed the checksum above.
`tests/rom_test.cpp` compares the two:

    the built-in cartridge equals build/arkanoid.rom (32768 bytes)

Embedding the whole image rather than extracting tables one by one is
deliberate: the original addresses its data by computed pointers into the
cartridge (`LEVELS_PTR_TABLE + 2*LEVEL`, `LD (HL)` and friends), so keeping
the image and its addresses is both the shortest path to a faithful port and
the easiest thing to check, and the port reads the same byte at the same
address as the Z80 does.

## 13 November 2026: the port's own picture

`src/game/Screen` is the video memory and the eight registers; `src/game/
Renderer` draws the 256x192 picture from them: Graphic 2 with a foreground
and background colour per cell row, three 2 KB pattern blocks, and 32
sprites of 16x16 with colour 0 transparent, lowest sprite number on top. It
is written from the TMS9918A datasheet, independently of the reference
machine's VDP, so the comparison below is between two implementations.

`tests/render_test.cpp` runs the original, copies its video memory and
registers into the port's `Screen`, renders with the port's `Renderer` and
compares every pixel with the reference machine's own rendering:

| Screen | Pixels compared | Differ, 4 sprite limit | Differ, every sprite |
|---|---|---|---|
| Title (300 frames) | 49152 | 0 | 0 |
| Level 1 (900 frames, after START) | 49152 | 0 | 0 |

That covers both colour tables, the pattern blocks, the palette, the sprite
patterns, transparency and priority. It found one real bug on the way: the
first version of `_RenderSprites` drew 16x16 sprites 32 pixels wide (24
pixels wrong around the sprite in the logo), now fixed.

## 13 November 2026: the ball

`src/game/State` is the game's memory: one 64 KB array addressed exactly as
the Z80 addresses it, and reads at cartridge addresses are served from the
built-in ROM image. So a table at `0x9A98` in the disassembly is the table at
`0x9A98` in the port, and the port reads the same bytes the Z80 fetches.

`src/game/Ball` holds the first routines ported out of the original, each one
keeping its original name and address:

| Port | Original | What it does |
|---|---|---|
| `UpdateSpeed` | `UPDATE_BALL_SPEED` 0x9AF0 | speeds the ball up after enough bricks |
| `CheckBouncesAndChangeSkewness` | `CHECK_BALL_BOUNCES...` 0x99D1 | every 40th border bounce, the skewness goes to the next value of the cycle |
| `VerticalBounce` | `BALL_VERTICAL_BOUNCE` 0x9B5B | inverts the vertical speed and mirrors the skewness about 9 |
| `HorizontalBounce` | `BALL_HORIZONTAL_BOUNCE` 0x9B80 | the same across |
| `InvertVerticalSkewness` | `INVERT_BALL_VERTICAL_SKEWNESS` 0x9B8A | the mirror on its own |
| `CheckGlueAndSkewness` | `CHECK_UPDATE_BALL_GLUE_AND_SKEWNESS` 0x9BA8 | the ball on the Vaus: angle from which seventh was hit, or glue when sticky |
| `OutBelow` | `BALL_OUT_BELOW` 0x9B2A | the ball is lost: an extra ball, or the Vaus explodes |

plus their callees `_ChangeBallsSkewness` (`CHANGE_BALLS_SKEWNESS` 0xAB38),
`_AddSound` (`ADD_SOUND` 0x5BEF) and `_DivideHlByC` (`DIVIDE_HL_BY_C`
0xB39A, the original's own division).

`tests/routine_diff.cpp` is the differential test: it runs the cartridge to a
real game state, then calls one routine at a time on the reference machine
(ix and iy pointed at that ball's table and sprite, interrupts off, a sentinel
return address to stop at) and in the port, from the same memory, and compares
the game variables from `0xE000` to `0xE600` byte for byte. Eleven states per
routine and ball, chosen to reach every branch: both signs of skewness and of
speed, steep and shallow angles, the 40th bounce, a sticky Vaus, an enlarged
Vaus and a full sound queue.

    209 of 209 routine calls agree with the original

To show that the test can fail, I changed the `sub 9` of
`BALL_VERTICAL_BOUNCE` to `sub 8`: 33 of the 209 calls then disagreed and the
tool reported them, and with the line restored all 209 agree again.

## 13 November 2026: the tick, its input stage

`src/game/Input` is the front of the frame, ported from `VDP_HOOK_HANDLER`
(0x424A): the three key-matrix reads packed into `KEYBOARD_INPUT`, `CONTROLS`
worked out as the keys pressed this frame rather than held, both cheat
sequences, and then the fork the original takes - START at the title means
cursor keys from then on, anything else reads the paddle. The paddle's own
two peripherals (its 9-bit count and trigger line) are the platform's, so
`Update` takes them as arguments; everything the game decides on them is the
original's code.

The original's shifting of the matrix rows into `KEYBOARD_INPUT` is written in
the port as the layout it produces (bit 0 up, 1 down, 2 left, 3 right, 4 SPACE,
5 GRAPH, 6 STOP) rather than as the same rotate-and-complement dance, because
that is what the rest of the game tests for.

`tests/input_test.cpp` runs 400 real frames of the original with a scripted key
pattern that cycles through every combination of the cursor keys, SPACE, GRAPH
and STOP. The port gets the keys and, as the platform would supply, the paddle
values the original read from the PSG; then the eleven bytes the input stage
owns are compared after every frame:

    4400 of 4400 bytes agree with the original over 400 frames
    (131 frames in play, 0 without a handled interrupt)

The state has to be taken at the handler's entry rather than at the start of
the frame: when a level starts, the game clears its variables between the two,
so a frame-start snapshot made one frame disagree (CONTROLS 0x60 against
0x20). `Machine1::onStep` finds the handler's entry and its return, and the
test compares exactly what the handler was given and what it produced.

The transition bodies the tick dispatches to - the title screen, the level's
objects, the next-level load - and the sound queue are still to come; they are
what `GAME_TRANSITION_JUMP_TABLE` names, and each is a port of its own.

## 13 November 2026: the bricks' presence

`src/game/Brick` is `BRICK_EXISTS_AT_ROWCOL` (0xADA8), the lookup the whole
collision routine is built on. The game keeps no byte per brick: the 12x11
grid is packed into 17 bytes at `0xE027`, and which byte and which bit a cell
uses comes from two tables in the cartridge, both of which the port reads at
their own addresses instead of retyping them:

- `BRICK_LEVEL_BITMASK` (0xAC10) is a 12x11 grid of 16-bit pointers into
  those 17 bytes, `32*row + 2*column` bytes in;
- `BRICK_BIT_CHECK_JUMP_TABLE` (0xAE01) names one of eight tiny handlers per
  cell, and the handler holds the bit: the port reads the handler's own
  `res n,(iy+0)` or `bit n,(iy+0)` opcode and takes the bit number out of it.

The bits run diagonally across the bytes, so neighbouring bricks are often in
different bytes *and* different bits; that is why the port reads both tables
rather than computing an address from the row and column.

One thing the data says and the code does not: the table's entry for column
11 is `0x0000`, so asking for that column makes the original test a bit at
address 0x0000 — the BIOS page. The collision code only asks for columns 0 to
10, so the port does not model it and the test does not exercise it; it is
noted in `Brick.cpp` rather than silently imitated.

`tests/brick_test.cpp` asks the original for every cell of the field, for
rows past the end (the original clamps them) and for five bitmask patterns,
and compares the answer, `DOH_BEEN_HIT` and that the bitmask was not modified:

    715 of 715 brick lookups agree with the original

Changing the table's column stride from 2 to 4 makes the test fail, so the
comparison is doing its work.

## 13 November 2026: the collision's hit points

`src/game/Collision` holds the two routines that decide *where* the ball met
a brick, which is what the collision's four direction cases are built on:
`COMPUTE_PRECISE_HIT_POINT` (0xA3D1) and `COMPUTE_WALL_ADJACENT_HIT_POINT`
(0xA591).

The original does not test the ball's centre. It builds an auxiliary slope
from the ball's skewness (`TBL_SPEED_FROM_SKEWNESS`, read at 0xA86C), steps
along it until the leading edge crosses the brick's boundary, counts the
sub-steps that took, and reconstructs the other coordinate at that moment.
The result goes into `BRICK_HIT_X_PIXEL`/`Y_PIXEL`, the ball gets placed
exactly on the surface, and the carry flag tells the caller which face wins.
Both routines keep their working values in the game's own variables
(`TICKS_TO_HIT`, `BALL_X_SLOPE`, `BALL_Y_SLOPE`, `HIY_Y_EDGE_A/B`,
`COMPUTED_HIT_X`), because the callers read them back.

`tests/hitpoint_test.cpp` builds 300 synthetic situations for each routine -
ball position, both speeds, skewness from -7 to 7, brick row and column - and
compares the whole game variable area and the carry flag with the original:

The corner cases followed, and they are the same walk once more:
`RESOLVE_CORNER_COLLISION` (0xA670), `HANDLE_CORNER_CASE_VERTICAL` (0xA810)
and `HANDLE_CORNER_CASE_HORIZONTAL` (0xA901). The first decides which of two
bricks at a joint the ball met and says which face won through the carry; the
other two put the ball onto the brick's surface for a double impact. All three
share the sub-step walk, which is now one helper in the port.

`tests/hitpoint_test.cpp` covers all five routines with 300 synthetic
situations each:

    1500 of 1500 collision geometry computations agree with the original

Two mistakes of mine it caught, both worth recording because they are the kind
that would have been invisible otherwise:

- the nested branches of the corner classification were inverted in the port
  (the original stores the clamped X in the third branch, not the second),
  which showed up as `BRICK_HIT_X_PIXEL` 0x63 against 0x3b;
- I had assumed `HANDLE_CORNER_CASE_VERTICAL` started at 0xA800; it starts at
  0xA810, so the test was calling into the middle of the previous routine.

The arithmetic is sensitive to it: changing the +15 of `COMPUTED_HIT_X_NEG` to
+14 drops the earlier 600 of 600 to 300.

### The drawing layer, first half: the score display

The collision calls the brick effects, and the brick effects write to the
screen, so the screen has to be part of the port before any of that can be
proven. `Screen` already had the video memory and the BIOS-style helpers
(`FillVram`, `CopyToVram`); what was missing was the routines that use them.

`src/game/Hud.cpp` now has `DECODE_BCD_TO_ASCII` (0x5420), which splits three
packed bytes into six ASCII digits with the Z80's RRD, `REMOVE_HEADING_ZEROS`
(0x5431), which blanks at most five leading zeros, and `DRAW_SCORE_NUMBERS`
(0x53B9), which decodes both scores plus the zeros buffer and copies six
characters of each into the name table, in whichever of the two places
`SCORE_POSITION` asks for.

`tests/hud_test.cpp` runs all three against the original, and for
`DRAW_SCORE_NUMBERS` it compares the port's `Screen` with the VDP's whole 16 KB
as well as memory, with the name table randomised before every case so an
untouched byte cannot pass for a match:

    600 of 600 score display computations agree with the original

It caught one thing immediately: the port's `Screen` starts with its VDP
registers cleared, so its name base was 0 and the digits landed 0x1800 below
where the original puts them. Unit tests have to seed the registers from the
machine; the game itself sets them at start-up.

### The drawing layer, second half: the playfield

A brick is two characters of the name table inside a 2x2 background tile, so
erasing one means putting the background back. `src/game/Playfield.cpp` has
`ERASE_BRICK` (0xAB8F), which computes the brick's first character as
`0x1800 + 2 + 2*32 + 32*(row + 1) + 2*column`, picks one of four two-character
pairs from `TABLE_BACKGROUND_ERASE` (0xAD90) by the row and one of the two
halves of that pair by the column, and writes them where the original writes
them through SETWRT and the data port.

`Brick::Remove` is `CHECK_AND_REMOVE_BRICK` (0xABD3): the same table walk as
`ExistsAt`, with the jump table's handler in its clearing mode, `res n,(iy+0)`.
It clamps nothing and does not touch `DOH_BEEN_HIT`, because neither does the
original. This is what the brick effects call to take a brick out of the mask
before `DEC_BRICKS_CHECK_LEVEL_DONE` measures the level.

`tests/playfield_test.cpp` compares 532 situations - every brick position, the
whole 16 KB of video memory for the erasing - against the original:

    532 of 532 playfield computations agree with the original

Two things it found:

- `ERASE_BRICK` doubles the brick column in an 8-bit register (`sla e`), so a
  column above 127 wraps. The port had used a wider multiplication and differed
  for every such column; it now wraps too.
- `CHECK_AND_REMOVE_BRICK` jumps through its own table without clamping a
  row or column, so outside the 12x11 field the original executes whatever the
  table's neighbour happens to be - one out-of-range case ran away entirely and
  another cleared a bit in the sprite parameters. That is not behaviour to
  port, so the test asks that routine only about the real field, and the reason
  is written down next to it.

Still to come in the drawing layer: `ADD_BRICKS_TO_TILEMAP` (0x5C15) with the
`ADD_BACKGROUND_TO_TILEMAP` (0x5D9D) it calls first - the level's bricks and
background drawn at the start of a round. Erasing and removing, which is what
the collision needs, are done.

## What is not done yet

- The rest of the game: `UPDATE_BALL_POSITION` (the movement loop, which
  needs the brick collision), `CHECK_BRICK_HIT_AND_BOUNCE_BALL`, the tick and
  its transition state machine, Vaus, bricks, capsules, aliens, Doh, scores,
  text, levels, and the PSG driver (Phases 1 to 3).
- The RenkuOS target (Phase 4): not written, not built, not run.
- `game_diff`: the whole game, frame by frame against the original, once the
  tick exists.

### The brick effects, first half: which effect a brick asks for

Every level has a 132-byte table in the cartridge, one byte per brick, naming
one of five actions, and `TBL_BRICK_ACTION_TABLE_OFFSETs` (0xAA5C) says where
each level's table is. `APPLY_BRICK_HIT_EFFECT` (0xAA05) updates the ball's
speed, reads the offset and the brick's byte and jumps to the action.

`src/game/BrickEffects.cpp` does the read and reports the action as a number;
`tests/brickeffect_test.cpp` runs the original from 0xAA05 and steps until it
arrives at one of the five action addresses, so the action the port names is
compared with the one the original actually jumps to - for every brick of every
level, row and column clamped as the original clamps them:

    4224 of 4224 brick effect dispatches agree with the original

That covers all 32 levels x 12 rows x 11 columns, so the per-level tables are
proven rather than assumed. What is not done is the actions themselves: they
add points through BCD_UPDATE_SCORE_ADD_POINTS (0x538A) and the high-score
comparison, play a sound through ADD_SOUND (0x5BEF, a small ring buffer
honouring LASERS_FIRING), keep the hard-brick counters in
UPDATE_HARD_BRICK_TABLE (0x97AF), and spawn capsules in
SET_RANDOM_CAPSULE_IF_NO_EXTRA_BALLS (0x8560). Erasing and removing the brick,
which comes after all of that, is already ported and proven.

### The sound queue

`src/game/Sound.cpp` is `ADD_SOUND` (0x5BEF): nothing is queued while
`LASERS_FIRING` is set, otherwise the code goes into the ring buffer at
`SOUNDS_BUFFER` (0xE520) at the current count and the count advances, stopping
at 7 - the buffer has eight slots and the eighth is never used.

`BrickEffects::ResetUnusedVars` is `action_reset_unused_vars` (0xAA9C), the
action some levels give a brick, which clears the two shared variables.

`tests/sound_test.cpp` covers both over 400 situations each, including an empty
queue, a full one, the eighth slot and lasers firing:

    800 of 800 sound queue and variable resets agree with the original

### The brick effects, finished but for the capsule

The chain behind a brick is now ported: `GIVE_BRICK_HIT_POINTS` (0x9604),
`ADD_POINTS_AND_UPDATE_SCORES` (0x52A0), `BCD_UPDATE_SCORE_ADD_POINTS`
(0x538A), `CHECK_SCORE_LIFE_TARGET` (0x5319), `DRAW_LIVES` (0x71B9),
`DEC_BRICKS_CHECK_LEVEL_DONE` (0xAB7A), `UPDATE_HARD_BRICK_TABLE` (0x97AF) and
the actions `action_brick_hit`, `action_hard_brick_hit`,
`action_unbreakable_brick_hit` and `action_reset_unused_vars`, plus the bounce
path they share.

Two details worth keeping:

- the points are packed decimal added a byte at a time with DAA, so the port
  does the same two-step correction after each ADD rather than converting to an
  integer and back;
- the level is counted down *after* the points are given, and on the last brick
  the transition is armed and the brick is erased all the same, because the
  original falls through into ERASE_BRICK.

`tests/brickeffect_test.cpp` now compares the dispatch over every brick of every
level and the three actions over 300 random states each, in memory and in the
whole 16 KB of video memory:

    5124 of 5124 brick effect computations agree with the original

It found one wrong constant: `HARD_BRICKS_REMAINING_HITS` is 0xE039, not
0xE239. That also exposed a hole in the build: the Makefile did not make the
tests depend on the headers, so a changed constant was not rebuilt into them
and a stale binary passed. Both are fixed.

Not ported: `action_brick_hit_and_capsule` (0xAAC1), which is the capsule
feature - `SET_PROPER_RANDOM_CAPSULE_TYPE` (0xB028), `SET_RANDOM_CAPSULE_TYPE`
(0xB0DD), `DECIDE_BRICK_IS_LIFE` (0xB10A) and the three capsule tables - and not
part of the brick's own effect. `BrickEffects::PerformHitEffect` returns false
for it rather than doing something that would look right and not be.

### The capsule's randomness is a hardware artefact

Reading the capsule chain (`SET_PROPER_RANDOM_CAPSULE_TYPE` 0xB028,
`SET_RANDOM_CAPSULE_TYPE` 0xB0DD, `DECIDE_BRICK_IS_LIFE` 0xB10A,
`SET_RANDOM_FALLING_CAPSULE` 0xB0xx) turned up something that is not game logic:

    SET_RANDOM_CAPSULE_TYPE:
        ld a,r
        add a,c
        add a,b
        and 01fh

`ld a,r` reads the Z80's memory refresh register. Its value is whatever the
refresh counter happened to reach, and the code then adds `c` and `b` - the
values the previous routine happened to leave in those registers - before
masking. `SET_RANDOM_FALLING_CAPSULE` does the same with R, C, B and E.

So the capsule's type is a function of how many instruction fetches have
happened since the last interrupt and of register litter. It is not derivable
from the game's state, and the original is not deterministic here either: the
same game state can give a different capsule.

That has two consequences:

1. this is the one place where a port cannot be byte-comparable, because there
   is no byte to compare to - the reference machine's answer depends on its own
   timing;
2. a port needs its own source of the same kind (a counter or a PRNG) and can
   then reproduce everything downstream: which capsule appears, its sprite, how
   it falls, what it does when caught.

The deterministic parts are portable as they are: the one-in-33 gap between
capsules (`CAPSULES_LEFT`, 0xE023), the one-in-50 gap between lives
(`CAPSULE_LIVES_COUNTER_L/H`, 0xE025/0xE026), the "no capsule below four bricks
left" rule, the four type tables that exclude or include capsule types depending
on gluing, enlargement and lasers, and the `PORTAL_OPEN` and
`LIFE_OBTAINED_FLAG` refusals.

Waiting on a decision on which random source to use before writing it.

### The capsule: own random source, as decided

`src/game/Random.cpp` is the port's random source: a 16-bit linear congruential
generator of which the high byte is used, so it returns any byte with the same
spread as `ld a,r`. Its `SetFixed` is how the tests pin it. This is the one
deliberate divergence in the project, and it changes only which capsule of the
eight appears, never the odds or anything downstream.

`src/game/Capsules.cpp` has the chain: `SET_PROPER_RANDOM_CAPSULE_TYPE`
(0xB028) with its "no capsule below four bricks left" and "one in the air at a
time" refusals, the two gap counters (33 bricks, 50 lives) drawn with `& 0x1f`,
the "nothing" and "life" types 5 and 6 with their `0xff` exhaustion, the four
falling-type tables chosen by gluing, enlargement and lasers, the type-to-colour
table, and the sprite at eight pixels per brick row and sixteen per column.

`tests/capsule_test.cpp` runs five situations - the two refusals, the two
counter-determined outcomes and the portal open - and for each compares the
port with the original over the whole game variable area. For the table-drawn
type it asks whether one of the eight table entries reproduces the original,
which is all the information the original had:

    1000 of 1000 capsule spawnings agree with the original

It caught one mistake: the sprite's Y is three shifts and `+24`, eight pixels a
brick row, not four. The port had two shifts and put the capsule half a brick
row too high.

With that, every one of the five brick actions is ported and proven.

### The brick collision, started: the two edge helpers

`src/game/BrickCollision.cpp` now has `CHECK_BALL_REACHES_RIGHT_BORDER`
(0xA29A) and `CHECK_RARE_OR_IMPOSSIBLE_CASE` (0xA2AD). Both are gates into one
body at 0xA2BD: the ball in the same row as before meets the brick under it and
takes a horizontal bounce from `COMPUTE_PRECISE_HIT_POINT`; one row further on,
the far corner is resolved with `COMPUTE_WALL_ADJACENT_HIT_POINT`, and if that
finds no brick the same-row answer is used after all.

`tests/brickhit_test.cpp` compares them with the original over 400 states each -
ball position, both speeds, skewness, the two cells, the brick mask, the tilemap
- in memory, in the whole 16 KB of video memory and in the carry:

    800 of 800 brick collision helper calls agree with the original

### Not yet: CHECK_VERTICAL_DOUBLE_IMPACT

`CHECK_VERTICAL_DOUBLE_IMPACT` (0xA328) is transcribed and runs, but it
disagrees on 21 of 1200 situations: a sprite coordinate one pixel out in one
shape of the case, and one hard-brick counter decremented where the original
left it alone. Both look like one branch taken differently rather than a
misreading of the layout, but it is not fixed, so:

- `tests/brickhit_test.cpp` does not compare it - the test is not made to pass
  by loosening it;
- the method carries a NOT VERIFIED warning in both the header and the source,
  and nothing calls it.

That is where the work stands: the routines on either side of the main
`CHECK_BRICK_HIT_AND_BOUNCE_BALL` are proven, this one helper is not.

### CHECK_VERTICAL_DOUBLE_IMPACT: fixed, and the port was not the problem

The 21 disagreements were the test's fault, not the routine's.

The failing cases all had `PREV_BRICK_X` = 11 and, in one, `CURR_BRICK_Y` = 12.
Both are values the four cases of `CHECK_BRICK_HIT_AND_BOUNCE_BALL` reject
before they ever call this helper: each case gates on `PREV_BRICK_X < 11` and
`CURR_BRICK_Y < 12` first. Fed column 11, the original looks up the brick mask
through the cartridge's entry for that column, which is 0x0000 - the BIOS page -
so it was answering from whatever its own low memory held, and the port answered
from its model of the same (empty) entry. There was nothing to agree about.

The diagnostic that settled it: the corner resolver was run on its own from the
same state and returned carry 1 on both sides, which meant the flow agreed up to
the point where the port and the original each asked about a brick at column 11
and got different answers out of the machine's page zero.

With the test kept inside the domain - `PREV_BRICK_X` 0 to 10, `CURR_BRICK_Y`
0 to 11, `PREV_BRICK_Y` up to 12 because that is what the double-impact case is
looking for - all three helpers agree:

    1200 of 1200 brick collision helper calls agree with the original

The "NOT VERIFIED" notes are gone from the header and the source, and the test
compares all three routines rather than two.

Worth recording as a rule: a differential test has to stay inside the domain the
original is ever called with, or it starts comparing undefined behaviour. The
same thing happened earlier with `CHECK_AND_REMOVE_BRICK` and out-of-range rows.

### CHECK_BRICK_HIT_AND_BOUNCE_BALL: read, not yet written

The three helpers are proven (1200 of 1200). The main routine is read to within a
few branch bodies, and what it does is now clear:

- four quadrant cases, one per diagonal direction, each gated on the sign of the
  two speeds;
- each computes `CURR_BRICK_Y = (spriteY - offset) >> 3`, `CURR_BRICK_X =
  (spriteX - offset) >> 4`, then the previous cell from the position one step
  back (`PREV_Y_PX = spriteY - ySpeed`, `PREV_BRICK_Y = (PREV_Y_PX - offset) >>
  3`, and the same for X), rejecting anything outside the field;
- the offsets are 24/12 for the two right-going cases and 24/17, 19/12, 19/17 for
  the others, which is why the four cases compute the same cell four ways;
- two of the cases call `CHECK_BALL_REACHES_RIGHT_BORDER`, the other two
  `CHECK_RARE_OR_IMPOSSIBLE_CASE`;
- then the row comparison decides between same row, one row on and done, and the
  column comparison between same column, one column on and done, each leading to
  one of: vertical bounce on the cell ahead, horizontal bounce with the corner
  handler, corner resolution, or nothing.

Still unread, and the reason this is not written yet: the bodies of
`up_right_check_top_face`, `up_right_check_corner_horizontal`, the two branches
of `up_left_same_row` and the last two of the down-left case. They are short -
each is a row/column pair, an existence test, one handler and a bounce - but
guessing them would be exactly the invention this project has avoided, and the
routine is one unit: half of it is not worth having.

### CHECK_BRICK_HIT_AND_BOUNCE_BALL: ported and proven

The whole routine is written, and it is one routine rather than four: the two
speed signs choose the cell offsets and which of the two edge helpers to call,
and after that all four directions ask the same questions in the same order -
same row or one row on, same column or one column across - and give the same
four answers (bounce off the face ahead, bounce with the horizontal corner
handler, resolve the corner, or nothing). That is `_Octant` here, with
`_FaceAndBounce` and `_CornerPair` for the answers.

Three things the reference had to teach, each now a comment in the code:

- the address. The routine starts at 0x9C2D, not 0x9C4C; the call at 0x9A5D
  says `cd 2d 9c`. Starting it at 0x9C4C - the `cp 12` inside it - ran garbage
  and produced the first 52 failures of the test.
- the wrapping subtract. `sub 24` and `srl a` are eight-bit operations; a sprite
  above the field's origin gives 0xF9, not -7. Shifting an `int` gives -1 and a
  C++ cast then reads 0xFF, one column off in the wrong direction.
- the previous-row gate belongs to the up-going cases only. A ball going up has
  come from row 12 at most; a ball going down has its previous cell above it, so
  the only value out of range is 31, and 31 is exactly the double impact the
  routine is looking for. Rejecting it before the test made every double impact
  impossible.

    999 of 999 brick hit checks agree with the original

The checked memory is the game's variables and the whole video memory. Four
bytes are excluded on purpose: CAPSULES_RANDOM_NUM, the capsule lives counter,
CAPSULE_TYPE and the falling capsule's sprite colour all come from the capsule,
whose type is drawn from `ld a,r` - the one deliberate divergence. With the
capsule disabled the rest of the routine is byte-exact.

### UPDATE_BALL_POSITION (0x99DF): read in full, blocked on two routines

It is short and now completely read, so it is written down here rather than
half-ported:

    speed table = TBL_PTR_BALL_SPEED_PER_SKEWNESS_PTR[|skewness|]
    entry       = speed table[2 * SPEED_POSITION]
    SPEED_MULTIPLIER = entry[0], MOVE_TARGET = entry[1]
    if (++MOVE_COUNTER < MOVE_TARGET) return          ; most frames
    MOVE_COUNTER = 0
    Y_SPEED, X_SPEED = TBL_SKEWNESS_[NEG_]POS_TO_XY_SPEED[|skewness| - 1]
    b = SPEED_MULTIPLIER + 1
  loop:
    spriteY += Y_SPEED; spriteX += X_SPEED
    DOH_BEEN_HIT = 0
    LEVEL == 0x20 ? CHECK_DOH_HIT_AND_BOUNCE_BALL
                  : CHECK_BRICK_HIT_AND_BOUNCE_BALL
    if (DOH_BEEN_HIT) return
    djnz loop
    CHECK_ANY_BALL_HITS_ALIEN

So one "ball step" is really up to sixteen small moves, each followed by a brick
test, which is why the ball crosses a brick row without ever being inside one.

It is not written yet because its two ends call CHECK_DOH_HIT_AND_BOUNCE_BALL
(the last level) and CHECK_ANY_BALL_HITS_ALIEN (every step), and neither is
ported. A version that skipped them would move the ball and be wrong the moment
an alien was on screen, and nothing in this project is allowed to be that.

### The aliens, Doh and the ball's movement

Three more routines, and the ball can move.

    CHECK_ANY_BALL_HITS_ALIEN        0x79A5
    CHECK_BALL_HITS_ALIEN            0x79FD
    CHECK_DOH_HIT_AND_BOUNCE_BALL    0x967B
    UPDATE_BALL_POSITION             0x99DF

    motion_test: 800 of 800 alien, Doh and movement calls agree with the original

What the reference says, and what had to be got right:

- the ball's box is not symmetric: sixteen pixels to the left and up from its
  sprite position, four to the right and down. The alien is a single point.
- an alien is "active" only when its table byte is exactly 1 and its colour is
  not zero; hitting one costs five points, sets the entry to 2 (active and
  exploding) and the byte after it to 1, and the ball's vertical skewness is
  inverted on the way out - the hit *is* the direction change.
- Doh's face is the box (76,19)-(128,119). The side is read from where the ball
  was before the step, and the ball is pushed back to 75, 129, 18 or 119 so it
  never enters the box. Sixteen hits finish it: the sixteenth sets DOH_TABLE,
  takes all three balls off the field and queues the defeat sound.
- the small moves. `UPDATE_BALL_POSITION` waits MOVE_TARGET frames on the move
  counter and then takes SPEED_MULTIPLIER + 1 steps of the direction's speed,
  testing the bricks after each. The direction's speed is re-read from the table
  for every step, so a bounce in the middle of a frame changes the rest of that
  frame, not the next one. DOH_BEEN_HIT ends the frame at once; otherwise the
  aliens are tested once, after the last step.

`BallMotion` owns the pieces - the ball, the brick collision and the aliens - so
that one call moves one ball the way the original does. Nothing drives it from a
frame loop yet: that is the game loop, which is not written.

### Vaus, the tick, and the RenkuOS window

**Vaus** (`EXECUTE_VAUS_ACTION`, 0x68C4) is ported for the two states that a
round spends nearly all its time in - waiting, which becomes KEEP in one tick,
and KEEP itself: follow the demo's ball, follow the controls through
`TBL_INCREMENT_POS_VAUS_CONTROLS`, or follow the paddle, then the two limits (152
on the right, 8 at either edge) and the four sprites.

    vaus_test: 400 of 400 Vaus actions agree with the original

That test found a real bug in the port's own memory map: `SPR_PARAMS_BASE` is
0xE0CD, not 0xE0C9. 0xE0C9 is the scratch sprite slot the capsule workaround
uses, four bytes before Vaus; the constant had been named after the wrong one and
nothing used it until now.

Not ported yet, and marked as such in `Vaus.h`: enlarging, shrinking, lasering,
un-lasering, exploding and the crossing of the portal. Those are read, but each
builds one of four different four-sprite layouts and none has been checked, so
`ExecuteAction` returns false for them instead of guessing. `LASERS_STEP` is not
ported either.

**The tick** (`Game::Tick`) follows `VDP_HOOK_HANDLER`: input, the sprite upload,
then the transition. `TRANSITION_PLAY_LEVEL` is wired to what exists - Vaus, one
step per active ball, the score - in the original's order. `TRANSITION_START_LEVEL`
(the title, the story, the demo) and `TRANSITION_NEXT_LEVEL`
(`NEXT_OR_SAME_LEVEL`: next level, life lost, ending) are the level loader, which
is not ported, so those two cases do nothing and say so. The PSG driver's
per-tick step is missing for the same reason: `ADD_SOUND` fills the queue but
nothing plays from it yet.

**The window** is `src/platform/PlatformRenku.cpp`: a `BApplication` and a
`BWindow` with one `BView`, a 256x192 `BBitmap` filled from `Game::Frame` and
stretched with `DrawBitmap`, a `BMessageRunner` at 16667 microseconds for the
sixty ticks a second, and key events turned into the MSX keyboard matrix (rows 7
and 8) that `Input` reads.

**This file has not been compiled.** The worktree is Linux and has no Be
headers, so it is written against the API and must be built on RenkuOS. The
portable part, `Game.cpp`, does compile here and is covered by `make`.

### The level loader and the sound driver: read, not written

Both were started and neither is finished, so neither is claimed.

**The level loader** is not one routine but a course of them, and here is the map
so the next session does not have to find it again:

    LEVELS_PTR_TABLE        0x5DEF   33 words, level n -> its 17-byte bitmask
    LEVEL_COLORS_PTR_TABLE  0x5E2F   (follows the above)
    level bitmaps           0x6615   33 x 17 bytes, level 0 first
    TBL_COLOR_TO_PATTERN    0x5DE5   21 bytes, colour -> two tile numbers
    BRICKS_PER_LEVEL        0x5DD3   32 bytes
    ADD_BRICK_TO_TILEMAP    0x5C6C   one brick: two characters, from the colour
    ADD_BACKGROUND_TO_TILEMAP 0x5D9A 12 x 22 cells from TABLE_BACKGROUND_ENTRY1
    DRAW_BACKGROUND_TILEMAP 0x5D77   the 12 rows of 22 into VRAM at 0x1862
    NEXT_OR_SAME_LEVEL      0x7B94   three transitions, then the reset that
                                     clears BRICK_MAP (1421 bytes), sets
                                     GAME_TRANSITION_ACTION and GAME_STATE to 0
                                     and DOH_RECEIVED_HITS to 0

`NEXT_OR_SAME_LEVEL` is read in full and is small: it dispatches on
LEVEL_TRANSITION_TYPE to ACTION_INC_LEVEL or ACTION_LIFE_LOST, both of which
delay, play a sound, and on the end of the game either run the ending text or
draw "GAME OVER" from a sprite table, then fall into the reset. What is *not*
read is the routine that puts a level's bitmask on the screen - the loop that
walks the 17 bytes, tests each bit and calls ADD_BRICK_TO_TILEMAP - which is the
part that has to be ported for a round to start.

**The sound driver** is `sound_src.asm`: 1,405 lines, 28 five-byte descriptors,
two concurrent streams with priority and chaining, and its own sequence
interpreter. `ADD_SOUND` (the queue) is ported and proven; nothing reads the
queue yet. Porting the player means porting a small virtual machine *and* a
per-tick comparison of PSG register writes, which is a phase of its own.

### The level loader: the field, proven

Three routines, and the field of a fresh level is byte-exact:

    ADD_BRICKS_TO_TILEMAP      0x5C15
    ADD_BACKGROUND_TO_TILEMAP  0x5D9D
    DRAW_BACKGROUND_TILEMAP    0x5D79

    levels_test: 600 of 600 level loader calls agree with the original

What they do: the background is a four-by-four block of characters tiled over
twelve rows of twenty-two cells; each level's 17-byte bitmask is walked bit by
bit, and for every bit set a colour is taken from that level's colour list - one
byte per brick, in the same order - and turned into two characters through
`TBL_COLOR_TO_PATTERN`. `BRICKS_LEFT` comes from `BRICKS_PER_LEVEL`, and the
hard bricks' strength is `LEVEL >> 3 + 2` written over all 132 cells. The result
is then copied to video memory at 0x1862, one 32-character line apart.

Four addresses in the earlier note were wrong and are corrected here; each was
found by the test failing, not by reading:

    BRICKS_PER_LEVEL        0x5D00   (was 0x5DD3)
    TBL_COLOR_TO_PATTERN    0x5DDB   (was 0x5DE5)
    ADD_BACKGROUND_TO_TILEMAP 0x5D9D (was 0x5D9A - the label, three bytes early)
    DRAW_BACKGROUND_TILEMAP 0x5D79   (was 0x5D77 - same reason)

Two smaller things the test taught: the background loops leave
`BRICK_HIT_Y_PIXEL` at 12 and `BRICK_HIT_X_PIXEL` at 22, because they are the
loop counters; and the brick walk leaves `BRICK_BIT_COUNT` at 0 and
`BRICK_BLOCK` at 17.

Not right yet: `LEVEL_TRANSITION_SAME`, a level replayed after a lost life. It
draws from `BRICK_MAP` - the bricks still standing - while it walks the colour
list of the level's own bricks, so the colours land on different bricks than the
original's. Fresh levels, which is what starting a round needs, agree byte for
byte, so `levels_test` compares those and `Levels.h` says why.

`Game::LoadLevel` is the pair of calls the original makes in that order.

### The replay case, fixed

`LEVEL_TRANSITION_SAME` is right now, and the reason is a detail only the
disassembly shows: the original does not trust the drawn map for the colours. On
every cell it decides *not* to draw, it re-tests the bit of the **level's own**
bitmask - the jump-table dispatch at 0x5C8D loads `LEVELS_PTR_TABLE[LEVEL]`
again, whatever the drawing source was - and advances the colour pointer when
that bit is set. So in a replay, a brick already destroyed still spends its
colour, and the colours of the bricks that remain stay where they were.

The port now does the same: one colour per brick the level has, drawn or not.

    levels_test: 1200 of 1200 level loader calls agree with the original

All three transitions - next level, next level again, and same level - over
every level the pointer table has.

### The round's colours and its number

Two more of the round start's calls, both small and now proven:

    COLORIZE_BACKGROUND  0x5BBD
    WRITE_ROUND_MSG      0x7204

    levels_test: 1500 of 1500 level loader calls agree with the original

`COLORIZE_BACKGROUND` fills each of the three 2 KB blocks of the colour table
with a single byte chosen from four by `LEVEL & 3` - which is why every level has
one flat colour behind the bricks. The last level (Doh) skips the lookup and
keeps the byte the previous block had.

`WRITE_ROUND_MSG` prints "ROUND" at [26, 22] and the level number at [29, 23] and
[30, 23] from a packed-decimal counter, with one leading zero: level 0 is
"ROUND 01". The tens digit prints as a space when it is zero.

Two more addresses were wrong and the test caught both:

    BACKGROUND_COLORS_TABLE  0x5BEB  (was 0x5BD3)
    ROUND_STR2               0x723C  (was 0x721E)

Both were found by searching the built cartridge for the bytes they must hold
(41 C2 41 81 and "ROUND"), which is the reliable way to place a table that the
disassembly labels but does not give an address for.

`Levels::CopyBrickMap` is the step I had said was missing: it copies the level's
17 bytes into BRICK_MAP, and is skipped for a replay. `Game::LoadLevel` now makes
the round start's calls in the original's order - colours, round number, map,
field, field to video memory. The lives, the sprite patterns and the music that
sit between them are not ported, and the title screen still is not.

### DRAW_LIVES, and where the round start now stands

`DRAW_LIVES` (0x71B9) was already ported inside `BrickEffects`; it is now part of
the round start and checked on its own:

    lives_test: 200 of 200 life displays agree with the original

It is a character in video memory for each life left, at the addresses in
`LIVES_VDP_ADDRESSES`, drawn from `LIVE_PATTERNS` - the little Vauses in the top
left corner.

The round start, in the original's order (0x4DEB onwards), is now:

    COLORIZE_BACKGROUND        ported, 1500/1500
    WRITE_ROUND_MSG            ported, 1500/1500
    DRAW_LIVES                 ported, 200/200
    BRICK_MAP copy (0x4E51)    ported
    ADD_BRICKS_TO_TILEMAP      ported, 1500/1500
    DRAW_BACKGROUND_TILEMAP    ported, 1500/1500
    UPDATE_SPRITE_PATTERNS_ON_LEVEL   not ported
    the sprite table for Vaus and READY   not ported
    the ball and the story flags (0x4EB4):
        BALL_TABLE1 active, BALL_X_DEMO 104, five words cleared,
        SOUND_NOP_0, one tick delay       not ported

Those last three are the only things between this and a round that starts. The
one at 0x4EB4 is small, but it calls `PLAY_SOUND` and `DELAY_HL_TICKS`, so it
cannot be checked the way the others are until the sound player exists - the
original would halt waiting for an interrupt the test machine never sends.

### The sound player: started

Read so far, and written down before it is lost:

    SOUNDS_BUFFER       0xE520   8-byte queue of codes, filled by ADD_SOUND
    SOUNDS_COUNT        0xE51E   how many are in it, kept in 1..7
    SOUND_NUMBER        0xE5C0   the code the player is asked to play
    SOUND_BUFFER_1      0xE5D3   the first stream's state
    SOUND_BUFFER_2      0xE5E9   the second stream's state
    TBL_SOUND_PARAMS    0xB5xx   five-byte descriptor per code (sound_src.asm)

The driver, by entry point:

    TBL_SOUND_PARAMS               sound_src.asm:7      28 descriptors
    SOUND_EFFECT_PRESET_TABLE      :104
    ADVANCE_SOUND_STREAM_IF_READY  :130   one stream, one tick
    PLAY_SOUND                     :202   the code → descriptor dispatch
    QUEUE_SOUND_DESCRIPTOR         :276   priority, then the buffer is set up
    WRITE_PSG_REG7_PRESERVING_IO_BITS  :431
    WRITE_MASKED_PSG_REG_AND_ADVANCE   :469
    WRITE_PSG_REG_VALUE            :484
    SOUND_ISR_UPDATE               :509   the per-tick step, both streams
    DISPATCH_PRIMARY_SOUND_COMMAND :656
    DISPATCH_SECONDARY_SOUND_COMMAND :671
    SOUND_CMD_HANDLER_BLOCK        :716   the command jump table
    CMD_SET_ONE_NOTE_ON_CHANNEL    :785
    UPDATE_PERIOD_EFFECT           :892
    UPDATE_VOLUME_EFFECT           :931
    UPDATE_DELAYED_REPEAT_EFFECT   :990
    INIT_EFFECT_FROM_PRESET        :1016
    LOAD_NEXT_EFFECT_PRESET_STEP   :1044
    UPDATE_MIXER_FROM_CHANNEL_MASK :1093
    CONFIGURE_CHANNEL_ENABLES      :1095
    SOUND_SEQUENCES                :1154   data

`PLAY_SOUND` itself is a dispatch on the code, and it is worth writing down
because it is not a table lookup: below 128 the descriptor index is `code + 6`
and the stream is buffer 1; from 128 to 191 it is `code + 144` on buffer 2; from
192 up it is `(code - 240 + 48) * 2 + 16` on buffer 1 and the same plus one on
buffer 2 - two descriptors at once, which is how the musical pieces get two
voices.

So the player is one small virtual machine: a descriptor, a preset table, a
six-byte command stream per effect, and handlers for period, volume and delayed
repeat. It is a phase of work of its own, not a routine, and it has not been
written yet.

### PLAY_SOUND, verified

The front half of the player is ported and agrees with the original:

    soundplayer_test: 300 of 300 sound queues agree with the original

Every code from 0 to 255, both streams in an arbitrary state, the sound
queue open or inhibited; the game's variables and all sixteen PSG registers are
compared after the call.

Two things the test taught me, which reading had got wrong:

1. **The descriptor pointer is read from 0xB400 + index, not from
   `TBL_SOUND_PARAMS`.** The `add a, 6` in the code below 128 exists only to
   cancel the six-byte gap between 0xB400 and the table, so the byte that is
   finally read is the table's entry for the code itself. I had the base wrong
   and every case with a code below 192 disagreed in the stream's second byte.

2. **Codes from 240 up are not descriptors.** They are a four-bit request that
   goes straight at the chip: bit 0 returns at once, bit 1 writes the byte after
   `SOUND_NUMBER` and raises bit 7 of `SOUND_VOICE_CONTROL`, bit 2 writes the
   byte after that, and if the bits go further the code takes PSG reg 7, keeps
   its own two bits (0xC0), ors in 0x3F and sends it. I had these falling into
   the two-descriptor branch and setting a stream's active flag, which the
   original never does.

The player writes its registers through a small `PsgSink` interface, with a
sixteen-byte shadow of its own when no sink is attached, because the reference
machine's PSG is not something the game can have on this OS.

What is still missing is the half that makes a noise: `SOUND_ISR_UPDATE`
(0xB594), `ADVANCE_SOUND_STREAM_IF_READY` (0xB47A) and the command handlers.
The streams are now filled in correctly and left to sit.

### The tick interpreter: reading done, transcription not

`SOUND_ISR_UPDATE` (0xB594) begins by writing the whole register image to the
chip. It takes `SOUND_REG_MASK` (0xE5C3), clears it, and then walks
`SOUNDS_REGS_BUFFER` from 0xE5C4 - one byte per PSG register - writing a
register only where the mask bit is set, through

    WRITE_MASKED_PSG_REG_AND_ADVANCE:  inc a / inc hl / rrc c / ret nc /
                                       ld e,(hl) / out (0xA0),a / out (0xA1),e

so the mask says which of the eight registers this tick is allowed to touch.
Then it takes reg 7 through `WRITE_PSG_REG7_PRESERVING_IO_BITS`, which keeps
bits 6 and 7 of the chip's own reg 7 (the two that are not ours) and ors in the
low five from memory - that is how the IO port direction bits survive.

After the registers it advances both streams:

    ld hl, SOUND_BUFFER_1 ; ld bc,(SOUND_PTR_1)
    call ADVANCE_SOUND_STREAM_IF_READY
    jr nc, ...            ; not ready: leave it
    ld (SOUND_BUFFER_1+9), a    ; the countdown the parser returned
    call DISPATCH_SECONDARY_SOUND_COMMAND    ; ...and keep dispatching

`ADVANCE_SOUND_STREAM_IF_READY` (0xB47A) is the small state machine: if the
stream is inactive, return; decrement the byte at +9; if it did not reach zero,
return; then walk the three bytes at +2..+4 - the follow-sound id, the chain
count and the repeat count - and either reload the stream from its descriptor
(the `jr lb54dh` into the middle of QUEUE_SOUND_DESCRIPTOR is the reload), step
to the descriptor that follows it at +5, or, when both counts are spent, clear
the stream's active flag and fall through into PLAY_SOUND so that the
follow-sound id it was carrying gets played.

`DISPATCH_PRIMARY_SOUND_COMMAND` / `DISPATCH_SECONDARY_SOUND_COMMAND` read the
next byte of the sequence, take its high nibble as a command (0 when bit 7 is
clear, which ends the tick and stores the stream pointer back) and its low
nibble as the command's parameter, and jump through the offset tables at
`TBL_PRIMARY_SOUND_CMD_ENTRY_OFFSETS` (0xB66E) into `SOUND_CMD_HANDLER_BLOCK`
(0xB676).

**Why it is not transcribed yet.** The eight writes and the two reg-7 writes do
not add up the way I first read them: counting the `djnz` loops gives register
0 for the first call and register 6 for the seventh, then one standalone call
reaches register 7 with the byte at 0xE5CB, and then `inc hl` and
`WRITE_PSG_REG7_PRESERVING_IO_BITS` seem to take register 7 again from 0xE5CC.
Two different bytes cannot both be reg 7, so either an instruction boundary or
a `hl` increment in my reading is wrong. PLAY_SOUND had exactly this kind of
off-by-one in it - the `add a, 6` that cancelled a six - and the way to settle
it is the same: build a harness that calls the original's ISR for N ticks from
a random register image and compare the chip and the RAM after each tick, then
let the disagreements point at the mistake. That harness is the next thing to
write; a guessed transcription would only produce a wrong note.

### The register stage of the tick, verified

The harness did its job. `tools/isr_trace.cpp` runs the original's ISR one
instruction at a time and prints every `out (0xA0),reg` / `out (0xA1),value`
pair, which turns the confusion above into a measured map:

    mask 01 -> regs 0,1      mask 10 -> reg 8       mask 80 -> regs 11,12,13
    mask 02 -> regs 2,3      mask 20 -> reg 9
    mask 04 -> regs 4,5      mask 40 -> reg 10
    mask 08 -> reg 6

One bit per *channel*, not per register, which is what the chip is: three tone
periods of two bytes, the noise period, three volumes and the envelope. Two
more facts came out of the same trace: the mixer (reg 7) is written between the
two halves, only when bit 7 of its byte is set, and its value keeps the chip's
own top two bits; and the byte past the image at 0xE5D2 has bit 3 meaning "send
reg 13 once more", cleared afterwards.

So the earlier contradiction was my arithmetic, not the code: the standalone
call after the `djnz` loop advances to reg 7 without writing it, because the
mixer goes through its own routine.

Now ported and checked:

    isr_test: 400 of 400 sound ticks agree with the original

with a random inhibit flag, a random mask and a random register image, comparing
all sixteen PSG registers and the game's variables. One more correction came out
of it: the chip only remembers four bits of the tone periods and volumes and
five of the noise period, so the player's shadow has to mask what it writes or
its idea of the registers drifts from the chip's.

Still missing from the interpreter: the two `ADVANCE_SOUND_STREAM_IF_READY`
calls, the command dispatch through `TBL_PRIMARY_SOUND_CMD_ENTRY_OFFSETS`, and
the three effect updaters at the end (`UPDATE_PERIOD_EFFECT`,
`UPDATE_VOLUME_EFFECT`, `UPDATE_DELAYED_REPEAT_EFFECT`).

### A tick harness that shows the sequence, and what it revealed

`tools/isr_multi.cpp` plays a sound on the reference machine and then ticks the
interpreter, printing both streams, the channel mask and the registers after
every tick. Sound 1 looks like this:

    play    s1 01 80 ff 01 01 29 b4 5d b8 01   mask 00  regs 00 .. 00 bf
    tick 1  s1 01 80 ff 01 01 29 b4 65 b8 7f   mask 11
    tick 2  s1 ..                        7e   mask 01  regs 73 00 .. be 0f
    tick 3                                    mask 00
    tick 4                                    mask 01
    tick 5                                    regs 73 ...
    tick 8                                    regs .. be 0e

Three things are visible in ten lines and none of them were in the
disassembly reading:

- the descriptor chain is followed in memory: the sequence pointer moves from
  0xB85D to 0xB865, so the first command consumed eight bytes, not five;
- the tick countdown is reloaded to 0x7F, not to 1 as the stream setup leaves
  it - the parser's own rearm value is 127 ticks;
- a note repeats every four ticks (mask 01 alternating with 00) while the
  channel A period in reg 0 toggles between 0x73 and 0x00.

That is exactly the behaviour a transcription has to match and cannot infer.
The harness prints it; the command handlers themselves are not written yet.

### All 256 traces captured

    tools/capture_traces.sh build/arkanoid.rom 30 > build/traces.txt

gives 16 128 lines - every sound code from 0 to 255, played and then ticked
thirty times, with both streams, the mask and the registers after each tick. It
is a generated file (1.2 MB, under build/, not committed); the script is.

Two kinds of code are already visible in it. Code 0 is a one-shot: the stream
goes from `01 f0 ff 01 01` to inactive (`00 .. 00`) on the second tick and stays
there. Code 1 keeps a live stream and repeats a note every four ticks, which is
the shape the handlers have to reproduce.

The handlers themselves are still not written.

### The stream advance is ported, and a wrong address found

The address in my notes for `ADVANCE_SOUND_STREAM_IF_READY` was 0xB47A, taken
from the order of the source file. It is wrong. Reading the `call` operands out
of the ISR's own bytes gives the real map:

    WRITE_MASKED_PSG_REG_AND_ADVANCE   0xB586   (called six times)
    WRITE_PSG_REG7_PRESERVING_IO_BITS  0xB570
    ADVANCE_SOUND_STREAM_IF_READY      0xB4B5   (called twice, once per stream)
    DISPATCH_PRIMARY_SOUND_COMMAND     0xB63E
    DISPATCH_SECONDARY_SOUND_COMMAND   0xB649
    UPDATE_PERIOD_EFFECT               0xB77B
    UPDATE_VOLUME_EFFECT               0xB79B
    UPDATE_DELAYED_REPEAT_EFFECT       0xB7CF

With the right entry point the port passed at once:

    advance_test: 400 of 400 stream advances agree with the original

covering everything the routine can do in one tick - inactive, countdown not yet
zero, a command waiting (carry set), the repeat reload, the chain step to the
descriptor five bytes on, and the end of the stream where the follow sound is
played in its place.

Worth writing down for whoever does the handlers: the routine's own tail is a
`jr` into the *middle* of QUEUE_SOUND_DESCRIPTOR, so the reload is not a copy of
that code but the same code - which is why `_Reload` in the port is one small
shared function rather than three near-copies.

### Review of the sound work, and one bug it found

Going back over `SoundPlayer::Advance` against the original turned up a line I
had invented: before playing the follow sound at the end of a stream I wrote
`CAN_ADD_SOUND = 1`. The original does not do that - it clears the stream's
active byte, puts the follow id in `SOUND_NUMBER` and falls straight into
PLAY_SOUND, leaving whatever the queue's own flag was. The tests passed anyway
because the flag is normally open, which is exactly how an invented line hides.
Removed, and `advance_test` is still 400 of 400.

Two other things worth writing down from the same pass:

- `QUEUE_SOUND_DESCRIPTOR` runs `di` and then `ret c` on the priority path, so
  the original returns with interrupts *disabled* when a low-priority sound
  arrives while a higher one plays. On hardware the next interrupt is lost. The
  port has no interrupt flag to lose, so it behaves as the game intends rather
  than as the code does; this is a divergence, and the only one in the player.
- the reload path is one piece of code shared by three callers (`jr` into the
  middle of QUEUE), which is why the port has `_Reload` rather than three
  near-copies.

All game sources compile clean under `-Wall -Wextra -Wshadow`.

### The handler block, in address order

Extracted from `sound_src.asm` by its own address comments, sorted. The
source file is written in jump-table order, so this is the first time the
block can be read as code. Entry points from the two tables:

    primary   0: 0xB6E1  1: 0xB71D  2: 0xB710  3: 0xB725
              4: 0xB735  5: 0xB745  6: 0xB753  7: 0xB761

    secondary 0: 0xB676  1: 0xB684  2: 0xB698  3: 0xB6AE
              4: 0xB6B9  5: 0xB6B7  6: 0xB6C6  7: 0xB6C4

Every entry ends at `lb759` (0xB759): mask |= d, return. The stream
pointer is relative - ix+11 is the period effect state, which is exactly
PERIOD_EFFECT_STATE_1 for the first stream and _2 for the second.

```
  b855  dw SOUND_SEQUENCES
  b67e  ld de,DELAYED_EFFECT_STATE_A_STREAM1
  b681  call INIT_EFFECT_FROM_PRESET
  b684  ld hl,DELAYED_REPEAT_STATUS
  b687  ld a,(hl)
  b688  and 01fh
  b68a  jr z,lb690h
  b68c  set 7,(hl)
  b68e  inc hl
  b68f  ld (hl),a
  b690  ld d,010h
  b692  ld a,e
  b693  ld (SOUND_VOLUME_CONTROL),a
  b696  jr lb70eh
  b698  ld hl,PERIOD_EFFECT_STATE_1
  b69b  bit 3,a
  b69d  jr z,lb6cdh
  b69f  ld de, 0x0801
  b6a2  call UPDATE_MIXER_FROM_CHANNEL_MASK
  b6a5  sub a
  b6a6  ld (DELAYED_REPEAT_STATUS),a
  b6a9  call lb6aeh
  b6ac  jr lb690h
  b6ae  ld (DELAYED_EFFECT_STATE_A_STREAM1),a
  b6b1  jr nz,lb67eh
  b6b3  ld (VOLUME_EFFECT_STATE_1),a
  b6b6  ret
  b6b7  or 010h
  b6b9  jr z,lb6c0h
  b6bb  ld (DELAYED_EFFECT_STATE_C_STREAM1),a
  b6be  or 080h
  b6c0  ld (DELAYED_REPEAT_STATUS),a
  b6c3  ret
  b6c4  or 010h
  b6c6  ld (SOUND_NOISE),a
  b6c9  ld d,008h
  b6cb  jr lb70eh
  b6cd  ld (hl),a
  b6ce  and a
  b6cf  ret z
  b6d0  set 7,(hl)
  b6d2  inc bc
  b6d3  ld a,(bc)
  b6d4  rlca
  b6d5  sra a
  b6d7  jr c,lb6dch
  b6d9  ld a,(bc)
  b6da  set 6,(hl)
  b6dc  inc hl
  b6dd  ld (hl),e
  b6de  inc hl
  b6df  ld (hl),a
  b6e0  ret
  b6e1  ld hl,SOUND_PERIOD_CONTROL_HI
  b6e4  ld d, 2
  b6e6  ld a,(ix+00bh)
  b6e9  and 007h
  b6eb  ld (ix+00ch),a
  b6ee  bit 6,(ix+00bh)
  b6f2  jr z,lb700h
  b6f4  ld a,(ix+00dh)
  b6f7  bit 7,a
  b6f9  jr z,lb700h
  b6fb  neg
  b6fd  ld (ix+00dh),a
  b700  ld (hl),e
  b701  inc bc
  b702  ld a,(bc)
  b703  dec hl
  b704  ld (hl),a
  b705  ld hl, SOUND_PARALLEL
  b708  ld a,(hl)
  b709  and d
  b70a  jr z,lb759h
  b70c  set 3,(hl)
  b70e  jr lb759h
  b710  ld hl,PERIOD_EFFECT_STATE_2
  b713  bit 3,a
  b715  jr z,lb6cdh
  b717  ld de,01002h
  b71a  call UPDATE_MIXER_FROM_CHANNEL_MASK
  b71d  ld d,020h
  b71f  ld a,e
  b720  ld (PSG_ENVELOPE_SHAPE),a
  b723  jr lb759h
  b725  bit 3,a
  b727  jr nz,lb72eh
  b729  rlca
  b72a  ld (SOUND_PARALLEL),a
  b72d  ret
  b72e  ld (COMPLEX_EFFECT_FLAGS),a
  b731  inc bc
  b732  ld a,(bc)
  b733  jr lb739h
  b735  rlca
  b736  rlca
  b737  rlca
  b738  rlca
  b739  ld (COMPLEX_EFFECT_PARAM_A),a
  b73c  inc bc
  b73d  ld a,(bc)
  b73e  ld (COMPLEX_EFFECT_PARAM_B),a
  b741  ld d,080h
  b743  jr lb759h
  b745  ld hl,AUX_TONE_PERIOD_HI
  b748  ld d, 4
  b74a  call lb700h
  b74d  ld de,DELAYED_EFFECT_STATE_A_STREAM2
  b750  call INIT_EFFECT_FROM_PRESET
  b753  ld d,040h
  b755  ld a,e
  b756  ld (SOUND_VOLUME),a
  b759  ld a,(SOUND_REG_MASK)
  b75c  or d
  b75d  ld (SOUND_REG_MASK),a
  b760  ret
  b761  bit 3,a
  b763  jr nz,lb76fh
  b765  ld (DELAYED_EFFECT_STATE_A_STREAM2),a
  b768  and a
  b769  jr nz,lb74dh
  b76b  ld (VOLUME_EFFECT_STATE_2),a
  b76e  ret
  b76f  ld de,02004h
  b772  call CONFIGURE_CHANNEL_ENABLES
  b775  sub a
  b776  call sub_b765h
  b779  jr lb753h
  b77b  ld e,(hl)
  b77c  bit 7,e
  b77e  ret z
  b77f  inc hl
  b780  dec (hl)
  b781  ret nz
  b782  ld a,e
  b783  and 007h
  b785  ld (hl),a
  b786  inc hl
  b787  ld a,(hl)
  b788  bit 6,e
  b78a  jr z,lb78fh
  b78c  neg
  b78e  ld (hl),a
  b78f  ld l,a
  b790  ld h,000h
  b792  rlca
  b793  jr nc,lb796h
  b795  dec h
  b796  add hl,bc
  b797  ld b,h
  b798  ld c,l
  b799  jr lb759h
  b79b  push hl
  b79c  pop ix
  b79e  ld c,a
  b79f  ld b,(hl)
  b7a0  bit 7,b
  b7a2  ret z
  b7a3  inc hl
  b7a4  dec (hl)
  b7a5  ret nz
  b7a6  ld a,b
  b7a7  and 15
  b7a9  ld (hl),a
  b7aa  inc hl
  b7ab  dec (hl)
  b7ac  jr z,lb7c1h
  b7ae  bit 6,b
  b7b0  jr z,lb7bbh
  b7b2  inc c
  b7b3  inc c
  b7b4  ld a,15
  b7b6  cp c
  b7b7  jr nc,lb759h
  b7b9  ld c,a
  b7ba  ret
  b7bb  ld a,c
  b7bc  and a
  b7bd  ret z
  b7be  dec c
  b7bf  jr lb759h
  b7c1  inc hl
  b7c2  inc hl
  b7c3  ld a,0f0h
  b7c5  add a,(hl)
  b7c6  ld (hl),a
  b7c7  dec hl
  b7c8  jr nz,LOAD_NEXT_EFFECT_PRESET_STEP
  b7ca  ld (ix+AUDIO_TABLE_IDX_ACTIVE), 0
  b7ce  ret
  b7cf  ld a,c
  b7d0  and a
  b7d1  ret z
  b7d2  ld hl,DELAYED_REPEAT_STATUS
  b7d5  ld a,(hl)
  b7d6  bit 7,a
  b7d8  ret z
  b7d9  inc hl
  b7da  dec (hl)
  b7db  ret nz
  b7dc  and 01fh
  b7de  ld (hl),a
  b7df  dec c
  b7e0  jr nz,lb7bfh
  b7e2  dec hl
  b7e3  ld (hl),a
  b7e4  jr lb7bfh
  b7e6  ld a,(de)
  b7e7  and a
  b7e8  jr z,lb803h
  b7ea  ld hl,SOUND_EFFECT_PRESET_TABLE
  b7ed  add a,l
  b7ee  ld l,a
  b7ef  ld l,(hl)
  b7f0  ex de,hl
  b7f1  dec hl
  b7f2  ld a,(de)
  b7f3  and 070h
  b7f5  ld (hl),a
  b7f6  dec hl
  b7f7  ld (hl),e
  b7f8  ld a,(de)
  b7f9  bit 7,a
  b7fb  jr z,lb806h
  b7fd  and 00fh
  b7ff  push af
  b800  call LOAD_NEXT_EFFECT_PRESET_STEP
  b803  pop af
  b804  ld e,a
  b805  ret
  b806  pop af
  b807  inc (hl)
  b808  ld e,(hl)
  b809  ld d,0b4h
  b80b  ld a,(de)
  b80c  ld e,a
  b80d  dec hl
  b80e  ld (hl),001h
  b810  and 0c0h
  b812  scf
  b813  rra
  b814  ld d,a
  b815  cp 0e0h
  b817  ld a,e
  b818  jr z,lb824h
  b81a  and 078h
  b81c  rrca
  b81d  rrca
  b81e  rrca
  b81f  inc a
  b820  ld (hl),a
  b821  ld a,e
  b822  and 007h
  b824  and 03fh
  b826  inc a
  b827  dec hl
  b828  ld (hl),a
  b829  dec hl
  b82a  or d
  b82b  ld (hl),a
  b82c  ret
  b82d  ld (ix+00bh),000h
  b831  ld a,(SOUND_VOICE_CONTROL)
  b834  ld l,a
  b835  ld a,(bc)
  b836  rrca
  b837  ld h,a
  b838  ld a,e
  b839  jr nc,lb83eh
  b83b  cpl
  b83c  and l
  b83d  ld l,a
  b83e  or l
  b83f  ld l,a
  b840  srl h
  b842  ld a,d
  b843  jr nc,lb848h
  b845  cpl
  b846  and l
  b847  ld l,a
  b848  or l
  b849  or 080h
  b84b  ld (SOUND_VOICE_CONTROL),a
  b84e  ld e,010h
  b850  srl h
  b852  ret c
  b853  pop af
  b854  ret
```

### The preset engine, and what the probe could not settle

`tools/preset_trace.cpp` was built to answer the one question that blocks the
handler block: what `INIT_EFFECT_FROM_PRESET` (0xB7E6) leaves behind, given the
stack tricks in its body. The call targets were re-checked against the ROM
bytes first, and 0xB7E6 is right (the `call` at 0xB681 is `CD E6 B7`).

The probe as written does not answer it, and single-stepping found out why:

    pc b7e6  a 00 de e5e6      ld a,(de)      <- a = state[0] = 0
    pc b7e8  a 00 de e5e6      and a          <- zero
    pc b803  a 00              pop af         <- pops my sentinel return address
    pc 00f0  a fe              ret            <- and returns into nothing

So the "no effect" case is not a defect in the probe's setup but a real property
of the routine: with `a` zero it takes the `pop af` path, which *discards the
caller's return address on purpose*, because every caller reaches it through a
`push af` of its own. Probing the routine on its own therefore cannot work: it
has to be entered the way the handler enters it, or not at all.

So the handler block stays unwritten, and this file is the honest record of why:
not that the code is unavailable, but that the last probe I built to settle it
did not produce a trustworthy answer, and the alternative was to guess.

### Probing the handlers the way the dispatcher calls them

`tools/handler_trace.cpp` enters each of the sixteen handlers with the frame the
dispatcher builds - stream in IX, command byte address in BC, parameter in A and
E, handler address in HL - and prints every byte in 0xE5C0-0xE5FE that changed.
The first results, matching the listing exactly:

    primary 0 param P: period low = next sequence byte, period high = P,
                       stream+12 = stream[11] & 7, stream+13 negated if its
                       bit 7 was set, mask |= 0x02
    primary 1 param P: envelope shape = P, mask |= 0x20
    primary 2 param P: period effect state 2 = P | 0xC1, +1 = P,
                       +2 = next sequence byte
    secondary 0 param P: as primary 0 on channel A (mask 0x01), then the
                       delayed-effect preset is set up and the volume control
                       gets the preset's value

The preset engine writes five bytes below the state it is given - for
`DELAYED_EFFECT_STATE_A_STREAM1` that is 0xE5E5 down to 0xE5E1 - which is the
"three bytes below the pointer" in the listing, seen for the first time.

The engine's arithmetic is not yet derived, so the preset-dependent commands
(secondary 0, 1, 2, 3 and primary 5, 7) still cannot be written.

### The command VM, written

The handler block is now in C++: all sixteen entries, the dispatcher, the preset
engine and the three updaters, in `SoundPlayer::_Command`, `Dispatch`,
`_InitEffect` / `_Step`, `_UpdatePeriod`, `_UpdateVolume` and `_UpdateDelay`.

Two things the differential test (`vm_test`) found within minutes of existing:

1. **The two dispatchers load each other's table.** `DISPATCH_PRIMARY` (0xB63E)
   uses `TBL_SECONDARY_SOUND_CMD_ENTRY_OFFSETS`, and `DISPATCH_SECONDARY`
   (0xB649) uses the primary one. The first stream therefore runs the commands
   the tables call secondary. With the tables the "right" way round, every
   command landed in the wrong handler and the channel mask came out as 0xA2
   instead of 0x11.
2. **`0xB705` is the opposite of how it reads.** `and d; jr z` means the
   parallel bit goes up only when the channel's bit is *already* set.

Current state of the test, which is in the tree but not in `make test` because it
does not pass yet:

    45 of 70 sound ticks agree with the original

Two discrepancies remain and both are in the same place - the voice control
register comes out 0xFE where the original has 0xBE, and the volume control 0x00
where the original has 0x0F - so they are almost certainly one mistake in
`_MixerMask` or in the volume the note command takes from its preset.

### Why a command cannot be tested on its own

`tests/command_test.cpp` tries to compare one command at a time: a fake sequence
in RAM, the original's dispatcher called with BC on the command byte. It cannot
work, and single-stepping shows why:

    pc b63e bc f0ff          DISPATCH_PRIMARY
    pc b63f bc f100          inc bc
    pc b640 bc f100 a cd     ld a,(bc)      the command byte
    pc b647 bc f100 hl b666  the secondary table, as documented
    pc b661 bc f100 hl b6b9  jp (hl)
    pc b6b9                  the handler
    ... and its `ret` goes straight back to the caller.

The handler returns to whoever called the dispatcher, and the dispatcher's
*continue* is the ISR's own `jr` loop at 0xB5D9 - it is not part of the
dispatcher at all. So a call to the dispatcher runs one command and returns, and
`SOUND_PTR_1` is never written.

The command has to be tested where it really runs, which is what `vm_test` does
by calling the ISR. `command_test` stays in the tree as the record of that, not
as a passing check.

### The command VM, closed

`vm_test` compares a whole tick - registers, streams, variables - for every sound
code. It went from 45 of 70 to 72 of 72 over the first six codes, and 7988 of
8054 over all 256, when four things were corrected. All four were found by
single-stepping the original rather than by reading it:

- `CONFIGURE_CHANNEL_ENABLES` (0xB831) turns one channel off and another on, and
  its two arms differ: the `and l / ld l,a` arm *replaces* L, the `or l` arm ORs
  into it. Written as one OR, a cleared bit came back. Voice control is now
  byte-exact.
- The handlers load their own `DE` (`0x1002`, `0x0801`, `0x2004`); the parameter
  is not what goes in them. `E` is then reused by the envelope and volume write.
- The same routine ends `srl h / ret c / pop af / ret`, so with the carry clear
  it returns *past* its caller and the rest of the command never runs.
- `INIT_EFFECT_FROM_PRESET` (0xB7E6) does the same for a preset of zero. The port
  read that as "volume zero" and silenced notes the original keeps.

The lesson is the one this project keeps relearning: `and l` modifying A, or a
`pop af` before a `ret`, changes the result, and reading the listing is not the
same as watching it run. `tests/vm_test.cpp` gained a `WATCH` mode for this: on a
mismatch it replays the tick instruction by instruction and prints the PC that
changed the byte. That is what identified 0xB693 as the only writer of the
volume, and 0xB7E6's second exit as the reason the port had a second one.

Two failures remain in the wider sweep (40 ticks, 256 codes), both at the end of
a sequence: the chain count at `0xE5D7` is one low, and the first stream's
volume-effect state at `0xE5E1` reads 0x81 where the original has 0x00. The tail
of `ADVANCE` is the place to look; `advance_test` does not cover it because it
runs idle streams. `make test` runs the six-code sweep, which passes.

### The stream tail, second pass

Two more things were wrong, and both were visible only once the sweep covered
codes the game does not play.

**The volume has three destinations, chosen by the mask bit.** `_Volume` was
writing 0xE5CC every time. The handlers' `D` is not just the channel bit:

    0xB690  ld d,0x10 / ld a,e / ld (0xE5CC),a   the first stream's note
    0xB721  ld d,0x20 / ld a,e / ld (0xE5CD),a   the second stream's mixer
    0xB74F  ld d,0x40 / ld a,e / ld (0xE5CE),a   the second stream's own note

So every note the second stream played was writing the first stream's volume
byte, and the first stream's own volume survived instead of being replaced. That
one change took the sweep from 4160 to 5106 of 5136.

**The comparison has to stay inside the cartridge.** 122 of the 256 codes have
descriptor bytes that are not sounds at all - the table is shorter than 256
entries - and their `B3`/`B4` point anywhere, including 0x00C1, which the Z80
reads out of the BIOS and the port cannot: the port has the cartridge and nothing
else. `vm_test` now skips those codes and says how many, instead of reporting a
mismatch the port could never avoid. The single-step trace is what showed it: the
original's `BC` at `ADVANCE` was 0x06C6 and the C-BIOS really does have 0x56 at
0x00C2, which is the "wait" byte the original loaded.

### What is left

`./vm_test build/arkanoid.rom 40 256` reports 5106 of 5136 (122 codes skipped).
The 30 that differ are all in the second stream's command state:

    18 x 0xE5F7   the second stream's volume-effect state: 0x81 where the
                  original has 0x00
     8 x 0xE5E1   the first stream's volume-effect state
     2 x 0xE5D7   the repeat count, one low
     2 x 0xE5C3   the register mask

The shape of it is that the delayed-repeat status exists **twice** - 0xE5E7 for
the first stream, which the handlers at 0xB686 use, and 0xE5F7 for the second,
which 0xB75E and its neighbours use - and the port has one set of delay variables
shared by both tables. Splitting them is the next move; `WTICK=<n>` in `vm_test`
prints the original's `PC`, `BC`, `HL`, `A` and the stream bytes at 0xB4BF,
0xB4C8, 0xB4D2, 0xB4E1, 0xB545 and 0xB54D for that tick, which is how both of
the above were found.

### The delayed-repeat split: not the answer

The hypothesis was wrong, and the address list says so plainly. The two bytes the
sweep disagrees on are `0xE5E1` and `0xE5F7`, which are **stream1 + 14** and
**stream2 + 14**: the per-stream volume-effect state was always separate. There
is no shared delayed-repeat variable to split. The only genuinely shared ones are
`0xE5E7`/`0xE5E8` (the first stream's) and the second stream's pair, and the
residue is not in them.

Two things in the primary handler block were wrong and are now right, measured
against the sweep as neutral:

- the last two entries were swapped: `0xB761` is the effect-preset command
  (which branches to the mixer when bit 3 of the parameter is set) and `0xB76F`
  is the mixer itself;
- the mixer's channel bit and its mask bit are different numbers. `ld de,0x2004`
  gives `CONFIGURE` `D = 0x20`, but the shared tail reloads `D` with `0x40`,
  which is what goes into `REG_MASK` and what selects the `0xE5CE` volume byte.

What is left is one behaviour: for some second-stream sequences the port reaches
`INIT_EFFECT_FROM_PRESET` with a preset in `0xE5FC` and the original does not, so
the port writes a level to `0xE5F7` (0x81) where the original leaves 0x00. Writing
the whole command byte instead of its low nibble to `0xE5FC` made the sweep worse
(42 failures), so the nibble is right; the difference must be in *when* the preset
is stored - which stream runs the command, and in what order. That is the thread
to pull next, and `WTICK` plus a watch on `0xE5FC` will show it in one run.

### The volume updater's last write was two bytes too high

`UPDATE_VOLUME` (0xB79B) ends two ways. When the step's sum is not zero it calls
`STEP`; when it is zero it does `ld (ix+0),0` - it clears **the flags byte the
routine started at**. The port cleared `state - 2`, the count, so the flags byte
kept its old value and the whole family of `0xE5E1`/`0xE5F7` mismatches fell out
of that one address.

With it: 5056 of 5086 became 5206 of 5212. The last six were codes that start two
voices, where the test's cartridge check only looked at the last active stream; a
two-voice code can have one voice in the table and the other in a descriptor byte
that is not a sound at all. Checking both streams gave 5352 of 5356.

### What is left: one mask bit

Four ticks differ, all `REG_MASK`, all by the bit `0x40` - the second stream's
mixer. The routine that sets it is `CONFIGURE_CHANNEL_ENABLES` (0xB831), and the
port reads its two channel tests as right shifts:

    B840: cb 3c   sll h      ; carry = bit 7 of h, not bit 0
    B850: cb 3c   sll h
    B852: d8      ret c

Trying the true `sll` fixed the mask and broke the voice byte for odd command
bytes (`0xBE` became `0xB6`), so something else in that routine is still read
wrong - most likely the initial `rrca` interacts with the shifts in a way the two
reachable models do not capture. The way to settle it is to single-step 0xB831
with a known command byte and a known voice byte, which is one small tool away.

### The sweep is in the suite, with its four mismatches on the record

`make test` now runs `./vm_test build/arkanoid.rom 40 256 4`: every sound code,
forty ticks each, with a budget of four mismatches. It prints the budget and the
count, and a fifth mismatch fails the build. The number is meant to fall to zero;
it is not a licence to drift.

`tools/mixer_trace.cpp` is the instrument for finishing it. It runs
`CONFIGURE_CHANNEL_ENABLES` (0xB831) on the reference machine with a chosen
command byte, `E`, `D` and voice byte, prints `A`, `H`, `L` and the carry at every
instruction, and says whether the routine returned to its caller or past it.

It settles one thing already: `0xCB 0x3C` is **SRL H**, not SLL. The carry comes
from bit 0, the port's right shift is correct, and the trace of command 0xA9
(e=01 d=08 voice=bf) reproduces 0xBE and the "returns past the handler" ending
exactly as the port models it.

So the four remaining ticks are not in the shifts. For code 197 the port's mask
reaches 0x31 and the original 0x71, and the missing bit is 0x40, which only the
primary table's shared tail sets. The port runs `d1 a8 f8 90 e0` across the two
streams for that tick and none of those sets it, so the difference is which
commands the streams run, not what a command does - the next trace is a dispatch
comparison, not another routine.

### The last four are an ordering question, not an opcode

`WCMD` (added to `vm_test`) prints the byte and address each dispatcher reads at a
watched tick, so the original's command stream can be compared directly. For code
197 tick 1 the two streams run exactly the port's commands — stream 1 `06 ad b1
81 15`, stream 2 `a8 f8 90 e0 07` — so no command is missing or extra.

Tracing `UPDATE_VOLUME`'s entry in the port shows where the mask bit goes. The
stream 2 call for that tick arrives with `flags 0x00`: the `e0` handler has already
cleared `0xE5F7`, so the updater returns at its first test and never reaches the
`mask |= 0x40` tail at 0xB759. The original ends the tick with the bit set and the
same `0xE5F7` value as the port, so within the tick it must have updated the volume
while the flags were still live — before that clear — and the two then agree again.

The ISR runs the dispatches and the two period/volume pairs inline in an order
0xB5EE onwards spells out; the port's tick interleaves them differently. That is
the remaining question, and it is a scheduling one, not a shift or an address.

### The tick order was not the difference

The ISR from 0xB5B6 to 0xB63D, read byte by byte, is the order the port already
runs: dispatch stream 1, dispatch stream 2, then stream 1's period and volume with
the delay, then stream 2's period and volume. `0xB5D9`/`0xB5F2` call the two
dispatchers in a loop that `pop af; ret` from a handler re-enters — that is how a
handler abandons the rest of itself — and the terminator jumps past the loop, so
there is nothing left to reorder.

`0xB4B5`, the check before each dispatch, turns out to be the wait: it returns at
once if the stream is inactive, decrements the tick byte at stream+9 and returns
while it is non-zero, and only then looks at the command at the sequence pointer,
setting carry to mean "dispatch this". The port folds that into `Advance`, and the
outcome for the four failing ticks is the same either way, so the difference is
still inside `UPDATE_VOLUME`'s flags byte, not around it.

The allowance therefore stays at four. Dropping it now would only make `make test`
fail on the difference that is already documented, which is worse than an honest
budget: the sweep does fail the build on a fifth mismatch, and I checked that.

### The missing bit has an owner, and it is the preset command

Tracing every write to `0xE5C3` and `0xE5F7` during one frame puts the mask write
exactly where the doubt was:

```
cmd@b64a bc bb07 byte e0        ; the preset command, stream 2
t1 pc b75d: e5c3 20 -> 60       ; mask |= 0x40, from inside that handler
cmd@b64a bc bb08 byte 07        ; the wait byte
```

So `0xE0` reaches the shared tail at 0xB753 and the port's version never did. Read
byte by byte, 0xB761 tests `bit 3,a` on the command, stores the command itself as
the preset at 0xE5FC, and its `and a` therefore never takes the "clear the status"
branch; the tail then stores `E` — the low nibble — into 0xE5CE as the volume and
or-s `0x40` into the mask.

Implementing that is a real step: with it, codes 139 and 197 agree on `0xE5C3`,
`0xE5CE` and the stream pointers, and the disagreement moves to `0xE5F7` — the
effect state, which the port ends at 0x85 and the original at 0x00. One byte
instead of three, on the same four ticks.

The change is not in the tree. It swaps one wrong byte for another rather than
closing it, and the entry model for `INIT_EFFECT` from this handler is the part
still unproven — the next step is to trace 0xB7E6 the way 0xB831 was traced here,
which will say whether the effect state write belongs to that call at all.

### INIT_EFFECT read exactly, and a table that wraps

`tools/effect_trace.cpp` runs 0xB7E6 on the reference machine with a chosen preset
and prints every instruction plus every change to the effect state bytes. With the
preset `0xE0` it reproduces the state the original leaves behind:

    0xB7EA  ld hl,0xB4AE
    0xB7ED  add a,l          ; low byte only: the table wraps, carry dropped
    0xB7EE  ld l,a
    0xB7EF  ld l,(hl)        ; block index
    0xB7F1  dec hl / ld (hl) = header & 0x70
    0xB7F6  dec hl / ld (hl) = block low
    0xB807  inc (hl) / read the step byte from 0xB400|block
    0xB810  and 0xC0 / scf / rra -> the top bits, kept for the status byte
    0xB824  and 0x3F / inc a   -> the level
    0xB82A  or that            -> status = level | top bits

`_Step` already builds that byte the same way; the one thing the port does not do
is the wrap at 0xB7ED. Wiring the wrap in changes five other codes as well, though,
which is the useful negative result: those runs reach 0xB7E6 with presets the game
never stores, so the sweep is comparing behaviour outside the domain the original
is called with. Any further work on this byte has to filter the presets first
rather than tune the routine against them.

The tree keeps the model that measures better on the in-domain codes. The last
step is building that preset list from the sequences themselves, exactly as the
cartridge check did for the sound codes.

### The presets the cartridge actually uses, measured

Watching `0xE5FC` and `0xE5E6` for every change across forty ticks of all 198
replayable codes, on the original, gives the whole set of effect presets the game
starts:

    0x01  0x03  0x0D  0x0E

Four values, all below 0x52. That settles the wrap at 0xB7ED as unobservable in
domain: `add a,l` only wraps once the preset passes 0x51, and nothing the cartridge
stores comes close. The earlier experiment that turned the wrap on and moved five
codes was therefore measuring behaviour the original is never asked to produce,
which is the rule this project has followed since the sound codes were filtered.

It also says what those presets are: `0x0D` and `0x0E` are the low nibbles of `ED`
and `EE`, so the preset in a command is its low nibble, which is what the port
already does. The command that sets `0xE5FC` is therefore `0xE0` plus a nibble —
and a nibble of zero is "no effect", not "effect zero".

What is still open is the entry each command index resolves to. The offsets at
0xB66E do not reconcile with the addresses decoded at 0xB710, 0xB72D, 0xB73D and
0xB761: at least one of those is a stub that falls into another, and the mapping
has to be measured — one command byte at a time, printing the first handler
address the dispatcher jumps to — rather than read off the table.

### The table, measured instead of read

`tools/table_trace.cpp` runs each command through both dispatchers on the original
and prints the first address each jumps to, which is the entry whatever the offsets
say. The result is not what the port had:

    index  DISPATCH_PRIMARY     DISPATCH_SECONDARY
      0    0xB676               0xB6E1
      1    0xB684               0xB71D
      2    0xB698               0xB710
      3    0xB6AE               0xB725
      4    0xB6B9               0xB735
      5    0xB6B7               0xB745
      6    0xB6C6               0xB753
      7    0xB6C4               0xB761

The second column is what stream 2 runs, and it is off by one at the end: index 6
is **0xB753**, the shared tail itself — "store E as the volume, or 0x40 into the
mask, return" — while the preset command is index **7** at 0xB761. The port had
those two the same way round as each other, and that is what the 0x40 bit was
about: `E0` is index 6 and never touches the preset byte at all.

With the entries corrected the sweep goes from 5352 of 5356 to 5432 of 5434, and
what is left is the noise bit (0x08) on two codes at one tick, set by stream 1's
index 7 at 0xB6C4. The budget in the Makefile is now two.

### The last two ticks are a different sound, not a different bit

Printing the port's own command stream for code 109 at tick 37 shows where it has
gone:

    port    tick 37 cmd a8 at 0xBA2A, then 06
    original tick 37 cmd bd at 0x2723, f4, fb, ec, 07

The original is reading a sequence in RAM at 0x2723; the port is still in the
cartridge at 0xBA2A. So the two streams diverged before this tick — the port is
playing a different sound, and the missing noise bit is a symptom of that, not a
mistake in how a command sets the mask.

That is why the budget stays at two rather than going to zero: closing it means
finding where stream 1's pointer goes its own way (a chain or repeat hand-off is
the likely place, since the tick counts have matched everywhere else), and the
mask will follow once the streams agree.

### Then the divergence is inside one tick

The whole of `0xE000`-`0xE600` is compared every tick, and the stream pointers live
in it — so at the end of tick 37 both sides point at the same place. The original
still managed to run `bd f4 fb ec` from `0x2723` during that tick and then be back.

The only way that fits is that a stream is dispatched **more than once per tick**:
0xB5D9 and 0xB5F2 call the dispatchers inside a one-instruction loop (`jr -5`), and
a sound queued while the tick is running is picked up by that loop straight away.
The port's `Tick` runs exactly one pass per stream, so a mid-tick queue waits for
the next tick — the sound still happens, one tick late, and the mask bit it would
have set in that tick is missing.

That is the last difference, and it is a loop shape rather than a value: the tick
needs to keep dispatching a stream while the same checks that `0xB4B5` makes say
there is something to do.
