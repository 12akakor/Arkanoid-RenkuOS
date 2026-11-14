# Code review

A pass over the whole tree, in the order the code runs. Each finding says what it
is and what was done about it; the ones left open say what evidence there is, so
the next reader does not have to take my word for it.

## Fixed

### The window byte order was red for blue

`src/platform/PlatformRenku.cpp`, `GameView::Draw`.

The palette holds `0xRRGGBB` and `B_RGB32` is little endian, so the four bytes of
a pixel are blue, green, red, unused. The code wrote red into the blue byte and
blue into the red byte, which would have tinted every screen of the game. Now
`rgb & 0xff` goes to byte 0 and `rgb >> 16` to byte 2.

This is the kind of mistake that survives a compile and a screenshot of a black
screen, which is why it is worth naming: nothing in the test suite can see this
file.

### The mixer routine was two operations collapsed into one

`SoundPlayer::_MixerMask` (`CONFIGURE_CHANNEL_ENABLES`, 0xB831).

It turns one channel off and another on, and the assembly's two branches are not
the same operation:

```
        and l        ; a = a & l
        ld l,a       ; L is replaced outright
        or l
        ld l,a
```

against, on the other arm of the same `jr`,

```
        or l         ; the value is merely OR'd in
        ld l,a
```

I had written the first arm as an OR too, so a cleared channel bit came straight
back. The voice control register now matches the original exactly (it was one bit
wrong: 0xFE against 0xBE).

The handlers also load their own `DE` - `0x1002`, `0x0801`, `0x2004` - rather
than passing their parameter through, and `E` is what the envelope write and the
volume write use afterwards. I had been passing the command's low nibble in both
registers.

### The routine's second exit is a jump out of its caller

`SoundPlayer::_MixerMask` again.

The routine ends `ld e,0x10 / srl h / ret c / pop af / ret`. With the carry set it
returns to the handler; with it clear the `pop af` throws away the handler's
return address, so the return goes to the dispatcher instead and the rest of the
command never runs. That is not an optimisation - it decides whether the command
sets its mask bit afterwards. `_MixerMask` now returns whether it abandoned.

### `INIT_EFFECT` has the same trick, and it is why volumes were zero

`SoundPlayer::_InitEffect` (`INIT_EFFECT_FROM_PRESET`, 0xB7E6).

A preset of zero jumps into the middle of the routine - `pop af / ld e,a / ret` -
which returns past the caller, so the command stops there and leaves the volume
of the stream alone. The port treated zero as "an effect whose value is zero" and
wrote it, silencing notes that the original keeps. `_InitEffect` now returns
false for that case and every caller stops.

The value is also not `Read(block + 1)`: it is what the step loader leaves in `E`.

All four of these together took the whole-tick comparison from 45 of 70 to 72 of
72 over the first six sound codes.

## Open, with evidence

### The stream tail is still wrong for some codes

`./vm_test build/arkanoid.rom 40 256` reports 7988 of 8054. Two failures remain,
both at the end of a sequence:

- `0xE5D7` (the chain count) is one lower in the port than in the original
  (`code 28 tick 15: original 04, port 03`), so the port gives up a chain of
  descriptors one repeat too early;
- `0xE5E1` (the first stream's volume-effect state) is `0x81` in the port where
  the original has `0x00` (`code 16 tick 37`).

`advance_test` passes 400 of 400, but it runs idle streams, so the tail of
`ADVANCE` (0xB4B5) - the repeat count, the chain walk, the follow sound - is not
covered by it. That is where to look next, and `vm_test` is already the harness
that shows it.

### `PlatformRenku.cpp` has never been compiled

There are no Be headers in this worktree. Everything above about it is reading,
not testing. On the target it needs at least: a build, a window that shows
something, and a check that the keys and the paddle reach the game.

It also never calls `Game::LoadLevel()`, so the field is empty on start; and
`Game::Tick` handles only the play transition, so the title screen and the
between-level transitions do nothing. That is porting work, not a defect, but it
is the difference between "the game starts" and "the game runs".

### Smaller things worth knowing

- `Game::_PlayLevel` writes `SCORE_POSITION = 1` every frame. The original sets
  it once at the start of a round. It is harmless while only the play transition
  exists, but it is not the original's behaviour and should go with the round
  start.
- `BallMotion::UpdatePosition` computes `magnitude = -skewness` for a negative
  skewness. If the byte were ever `0x80` the result is 128, and the direction
  table is indexed at `2 * (magnitude - 1)`, past the end of the table. The game
  stores skewness in the range -127..127, so it cannot happen as the code stands;
  a clamp would make that independent of the caller.
- `SoundPlayer::Tick` runs once per frame from the game's tick, not from a VDP
  interrupt. The original's sound runs at the display's field rate; the port's
  runs at whatever rate the host ticks at, so tunes play at a different speed
  than on the MSX. It is a divergence of the platform layer, not of the driver.
- `Capsules::SpawnFromBrick` sets `CAPSULE_IS_FALLING` before it knows whether a
  capsule appears. Only the falling-capsule update, which is not ported, clears
  it, so in the port one spawn blocks the next. Unreachable today because nothing
  calls it yet.

### The platform layer, against a core that is now verified

`src/platform/PlatformRenku.cpp` is the only file that knows the Be API, and it
cannot be compiled in this worktree. Read against the core — whose sound engine
now agrees with the cartridge for 154912 ticks and whose other routines agree
over thousands of calls — it is correct as far as it goes, and it stops well
short of the core's ability. The gaps, in the order they would be noticed:

1. **There is no sound, and no way to ask for one.** `Game` builds its
   `SoundPlayer` without a sink (`fSoundPlayer(fState)` in `Game::Game`), the
   constructor takes no argument and there is no accessor. `PsgSink` — the two
   methods `WritePsg` and `ReadPsg` — is exactly the seam the platform needs,
   and nothing in `Game.h` exposes it. Until `Game` can be handed a sink, the
   verified PSG behaviour is discarded on the last step. This half is a core
   change (one constructor parameter); the other half is a Be audio
   implementation, which needs the target.
2. **The paddle never moves.** `Tick(fKeys, 0, 0)` passes both paddle values as
   zero; they are the values the original reads from the joystick port, and
   `Input::_ReadPaddle` is written to use them. The platform has to derive them
   from the pointer (`GetMouse`, or `MouseMoved` for relative motion) and the
   button.
3. **The playfield starts empty.** `Game::Game` clears the state and resets the
   screen; nothing calls `LoadLevel()`, and the platform does not either. That
   is expected while the level transition is unported, but the window currently
   has nothing to show.
4. **Timing.** A `BMessageRunner` at 16667 us has no drift correction: if a tick
   overruns, the messages queue and the game runs late rather than skipping. The
   original is a fixed 60 Hz interrupt. The sound driver inherits whatever rate
   the platform ticks at, so this decides how faithful the tunes are.

Fixed here, because they are defects the file can be corrected for without the
target: the bitmap is checked with `IsValid()` before `Draw` writes through
`Bits()`; focus is asked for in `AttachedToWindow` rather than in the
constructor, the old runner is deleted first (that call can happen twice), and
losing focus now clears the key matrix so a held arrow cannot keep the paddle
moving; the stretch uses `B_FILTER_BITMAP_NONE` so the 3x upscale stays square
instead of being blurred.

Still needs the target, and cannot be settled by reading: the arrow keys are
compared against `B_UP_ARROW` and friends through `bytes[0]`, which is the usual
idiom but depends on what the input server actually delivers; that the window
opens at 768x576 and the picture is not clipped; that a 60 Hz `BMessageRunner`
holds rate under load; and everything about the audio path once one exists.

### The audio seam, opened

`Game` now takes a `SoundPlayer::PsgSink*` (default NULL, which is what the tests
use). The platform passes its own `PsgOutput`, which keeps the sixteen register
values the driver writes and answers the register-7 read the driver makes when
it takes the mixer over. So the writes now arrive somewhere outside the core
instead of being dropped: what is still missing is synthesis — turning that
register image into samples at the PSG's clock — and that is the part that needs
the target.

`Game.cpp` is not linked into anything on this side (the platform build happens
on the target), so `make test` now syntax-checks it: the constructor change is
compiled on every run rather than there being nothing to notice if it rots.

### The PSG, synthesised

`src/game/PsgSynth.{h,cpp}` is the chip the registers were always meant for: three
square-wave tone counters, the seventeen-bit noise shift register, the envelope
generator with its sixteen shapes, and the mixer byte that decides what each
channel hears. It knows nothing about Be, so it is built and tested here rather
than on the target.

`tests/psg_test.cpp` measures it instead of listening to it, which is the only
thing possible without a sound card:

    a fresh chip is silent                                 0
    register 7 reads back                             0x38
    channel A, period 254                             440.0 Hz
    channel B, period 127                             880.5 Hz
    channel C, period 508                             220.0 Hz
    volume 0 is silence                                    0
    noise makes a sound of its own                    10922
    the attack envelope climbs to full                10922
    the decay envelope starts loud, then falls to 0       0

The frequencies come from counting sign changes in a second against
`clock / (16 * period)`, so the counter and the clock agree by numbers rather
than by intent.

On the platform side, `PsgOutput` now holds the synth behind a `BLocker` (the
driver writes from the game thread, the node renders from the media thread) and
`AudioNode` is a `BSoundPlayer` at 44.1 kHz, sixteen bits, stereo, whose hook
fills each buffer from the synth. So the chain is closed: game tick → register
writes → samples → node.

What is still unverified, and needs the target: that `BSoundPlayer` starts and
keeps its rate, that the buffer size is not too small to avoid dropouts, and
that the levels are right in a real mix — three channels summed and divided by
three is a deliberate headroom choice, not a measurement of the chip's output
stage.

## The synthesiser checked against the chip's numbers

The synth was the one part of the port with nothing to compare against: the
cartridge's sound is a register write, and the audible result happens inside a
chip the reference machine does not model the same way. So the comparison was
made against the AY-3-8910 datasheet's own envelope drawings and measured
volume ladder, plus the reference machine's PSG as a second opinion.

What it found, in order of how wrong it was:

* **The envelope ran sixteen times too slowly.** `F(env) = clock / (256 * EP)`
  is the rate of a *whole sixteen-step cycle*, so a step lasts `16 * EP /
  clock`, not `256 * EP / clock`. The synth stepped once per cycle. It now
  divides by the same sixteen as the tone counters, and one step of an
  EP of 256 comes out at 101 samples - which is exactly `16 * 256 / clock`
  at 44.1 kHz.
* **Two envelope shapes were wrong.** Shape 09 fell and then held at full
  instead of silent; shape 0B fell and started over instead of jumping to full
  and holding. Both are now what the datasheet draws, and all sixteen shapes
  are checked against that drawing (`psg_test`, three cycles each).
* **The volume ladder was a guessed 1.5 dB a step.** It is now the measured
  table from the reference implementation, which is also what the chip's own
  curves show: quiet steps are much quieter than 1.5 dB would make them
  (level 1 is 1.37% of full, not 8.4%).
* **The reference machine's PSG stepped its envelope twice too fast** - it
  counted `clock / 8` ticks against `EP` where a step is `2 * EP` of those.
  Fixed, though it only ever affected that machine's audio: no test compares
  it. Its envelope *shape* sequence still does not agree with the datasheet
  drawings, and it is not used for anything the port depends on, so it is
  recorded here rather than chased.

`psg_test` ends at 24 checks: silence, register read-back, three tone
frequencies, volume 0, the volume-1 ratio, noise, an attack and a decay, and
the twelve distinct envelope shapes (0x00 and 0x03 are the same shape, as are
0x04 and 0x07). The platform's `AudioNode` hands the synth its frames; that
file still cannot be compiled here for want of the Be headers.


## The game's own sounds, rendered

`make sounds` runs `tools/render_sounds`, which plays each of the twenty-eight
codes in the cartridge's table on the reference machine, reads the chip's
sixteen registers after every sound tick, hands the changes to `PsgSynth` the
way the driver would, and renders a sixtieth of a second of audio per tick.
The WAVs land in `build/sounds`; the numbers are the point.

| code | ticks | seconds | peak | rms dB |
| --- | --- | --- | --- | --- |
| 0 | 2 | 0.03 | 0 | -100.0 | silent |
| 1 | 129 | 2.15 | 10922 | -17.7 |
| 2 | 129 | 2.15 | 10922 | -17.7 |
| 3 | 129 | 2.15 | 10922 | -19.2 |
| 4 | 3 | 0.05 | 9264 | -15.7 |
| 5 | 129 | 2.15 | 10922 | -17.1 |
| 6 | 42 | 0.70 | 9264 | -14.8 |
| 7 | 130 | 2.17 | 10922 | -15.3 |
| 8 | 129 | 2.15 | 10922 | -18.4 |
| 9 | 503 | 8.38 | 10922 | -13.7 |
| 10 | 30 | 0.50 | 10922 | -9.8 |
| 11 | 30 | 0.50 | 10922 | -9.8 |
| 12 | 86 | 1.43 | 9264 | -11.6 |
| 13 | 86 | 1.43 | 9264 | -11.1 |
| 14 | 26 | 0.43 | 9264 | -14.4 |
| 15 | 26 | 0.43 | 7506 | -13.1 |
| 16 | 255 | 4.25 | 9264 | -15.4 |
| 17 | 518 | 8.63 | 9264 | -12.9 |
| 18 | 148 | 2.47 | 9264 | -15.5 |
| 19 | 314 | 5.23 | 9264 | -12.6 |
| 20 | 59 | 0.98 | 9264 | -14.7 |
| 21 | 170 | 2.83 | 9264 | -23.4 |
| 22 | 131 | 2.18 | 9264 | -16.9 |
| 23 | 201 | 3.35 | 9264 | -12.0 |
| 24 | 2181 | 36.35 | 24758 | -14.5 |
| 25 | 7541 | 125.69 | 24758 | -9.0 |
| 26 | 264 | 4.40 | 20023 | -7.7 |
| 27 | 262 | 4.37 | 20023 | -8.2 |

What the numbers say:

* **All twenty-eight stop on their own** - no sound needed the twenty-thousand
  tick ceiling, which is the same result the tick-by-tick test reports.
* **Code 0 is silent, and the cartridge says so**: it leaves every channel's
  volume at zero with mixer `0xbf`, so the chip is asked for nothing audible.
  It runs for two ticks: it is the "stop everything" entry, not a sound.
* **Nothing clips.** The effects peak at 7506 to 10922 of 32767; the two long
  pieces (codes 24 and 25, 36 and 126 seconds) peak at 24758 because more than
  one channel is at full volume at once, and codes 26 and 27 at 20023. RMS
  runs from -23.4 dB to -7.7 dB.
* **The tones are the ones the registers ask for.** In every tick that plays
  one tone on one channel and nothing else, the crossings in the rendered
  audio match `clock / (16 * period)` to the one-crossing resolution of a
  16.7 ms window: 3659 of 3659.
* **104 ticks play a tone above 22 kHz.** The game uses period 1, which is
  111.9 kHz on this clock; at 44.1 kHz the synth cannot carry it and what
  comes out is aliasing. That is the one fidelity limit this comparison
  found, and it is a property of the sample rate rather than the chip's
  model: the same ticks would need a 224 kHz stream to be reproduced.
