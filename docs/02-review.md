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
