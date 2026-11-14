/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The sound queue.
//
// The game does not play a sound when it asks for one: it appends the code to a
// small ring buffer and the sound driver plays from it. The buffer holds eight
// codes, and the count is kept in the range 1 to 7, so one slot always stays
// empty. While the lasers are firing nothing is queued at all, which is how the
// original keeps the laser's own noise from flooding the queue.


#ifndef ARKANOID_SOUND_H
#define ARKANOID_SOUND_H


#include <stdint.h>

#include "SoundCodes.h"
#include "State.h"


class Sound {
public:
						Sound(State& state);

		// ADD_SOUND (0x5BEF).
	void				Add(uint8_t sound);

	// The twelve codes in IsQueuedSoundCode are the only ones the cartridge
	// ever queues. Add still does what the original does with anything else,
	// because matching it is the job, but it counts them: nothing in this
	// port should ever produce one, and a test can say so.
			int			OutsiderCodes() const { return fOutsiderCodes; }
			uint8_t		LastOutsider() const { return fLastOutsider; }

private:
			State&	fState;
			int			fOutsiderCodes = 0;
			uint8_t		fLastOutsider = 0;
};


#endif	// ARKANOID_SOUND_H
