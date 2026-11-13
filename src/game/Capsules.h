/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The capsules that fall out of some bricks.
//
// A capsule does not appear whenever a brick is hit: the game keeps two
// counters - one counting bricks down from 33, one counting down from 50 - and
// only when a counter runs out does it draw a random number to decide. Until
// then the numbers it drew earlier are consumed one hit at a time, which is how
// a capsule's appearance is spread out.
//
// The type is chosen from one of four tables, which differ in which types they
// leave out, depending on whether a capsule is already falling, whether the
// vaus is sticky, enlarged or has lasers.


#ifndef ARKANOID_CAPSULES_H
#define ARKANOID_CAPSULES_H


#include <stdint.h>

#include "Random.h"
#include "Screen.h"
#include "State.h"


class Capsules {
public:
						Capsules(State& state, Screen& screen, Random& random);

	// SET_PROPER_RANDOM_CAPSULE_TYPE (0xB028).
	void				SpawnFromBrick();

	// The two counters, at 0xB0DD and 0xB10A. Both return true when the
	// brick's capsule has been decided and nothing more should be drawn.
	bool				SetRandomCapsuleType();
	bool				DecideBrickIsLife();

	// SET_RANDOM_FALLING_CAPSULE (0xB0AD): one of the eight entries of the
	// chosen table becomes the capsule's type.
	void				SetRandomFallingCapsule(uint16_t table);

private:
			State&		fState;
			Screen&		fScreen;
			Random&		fRandom;
};


#endif	// ARKANOID_CAPSULES_H
