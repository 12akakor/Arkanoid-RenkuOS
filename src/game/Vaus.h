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
// This is EXECUTE_VAUS_ACTION (0x68C4). Waiting, following the controls and
// being destroyed are ported and checked against the original frame by frame;
// enlarging, shrinking, lasering, un-lasering and the crossing of the portal
// are not, and the state is left where it is for those instead of being
// guessed.


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
	// VAUS_ACTION_STATE_EXPLODING (0x6E0E, vaus_destroyed): the Vaus falls
	// apart in four steps of twelve frames, and the last one sends the game on
	// to reload the level.
	void				_Destroy();
	uint16_t			_Sprite(int index);
	void				_PutSprites(uint8_t x);
	uint8_t				_ReadPaddle();
	void				_Store(uint8_t x);

			State&		fState;
			Screen&		fScreen;
			Sound		fSound;
			BrickEffects	fEffects;
};


#endif	// ARKANOID_VAUS_H
