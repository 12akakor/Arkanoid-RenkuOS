/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The paddle.
//
// Vaus is a state machine of ten bytes: an action state, a resizing step, a
// lasering step, two counters for its destruction, whether it is enlarged,
// whether it has lasers, and two steps for its passage through the portal.
//
// This is EXECUTE_VAUS_ACTION (0x68C4), with one honest gap: the states from
// enlarging on - enlarging, shrinking, lasering, un-lasering, exploding and the
// crossing of the portal - are not ported yet. They are read, but the sprites
// they build are four different layouts each and none of them has been checked
// against the original, so the state is left where it is instead of being
// guessed. Waiting and following the controls, which is what the game does for
// all but a few seconds of a round, is complete.


#ifndef ARKANOID_VAUS_H
#define ARKANOID_VAUS_H


#include <stdint.h>

#include "BrickEffects.h"
#include "Screen.h"
#include "Sound.h"
#include "State.h"


class Vaus {
public:
						Vaus(State& state, Screen& screen);

	// EXECUTE_VAUS_ACTION (0x68C4). True when the state was handled; false
	// for the states that are not ported yet.
	bool				ExecuteAction();

	// LASERS_STEP (0x69BA): not ported yet.
private:
	void				_PutSprites(uint8_t x);
	uint8_t				_ReadPaddle();
	void				_Store(uint8_t x);

			State&		fState;
			Screen&		fScreen;
			Sound		fSound;
			BrickEffects	fEffects;
};


#endif	// ARKANOID_VAUS_H
