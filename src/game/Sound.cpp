/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "Sound.h"


Sound::Sound(State& state)
	:
	fState(state)
{
}


// ADD_SOUND (0x5BEF). Nothing is queued while the lasers fire, the code goes
// into the buffer at the current count, and the count advances but stops at 7:
// the buffer has eight slots, so the eighth is never used.
void
Sound::Add(uint8_t sound)
{
	if (fState.Read(GameRam::kLasersFiring) != 0)
		return;

	const uint8_t count = fState.Read(GameRam::kSoundsCount);
	fState.Write(static_cast<uint16_t>(GameRam::kSoundsBuffer + count),
		sound);

	const uint8_t next = static_cast<uint8_t>(count + 1);
	fState.Write(GameRam::kSoundsCount, next == 8 ? 7 : next);
}
