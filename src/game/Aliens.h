/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The aliens, and Doh.
//
// Three aliens cross the field and can be knocked out by the ball; Doh is the
// last level's opponent, a wall of bricks with a face that the ball bounces off
// until it has taken sixteen hits.
//
// Both are kept as the original keeps them: the aliens in a table of twenty
// bytes each at 0xE4C7 with a sprite each at 0xE101, Doh's hits in one byte and
// its state in a course of flags.


#ifndef ARKANOID_ALIENS_H
#define ARKANOID_ALIENS_H


#include <stdint.h>

#include "Ball.h"
#include "BrickEffects.h"
#include "Screen.h"
#include "Sound.h"
#include "State.h"


class Aliens {
public:
						Aliens(State& state, Screen& screen);

	// CHECK_ANY_BALL_HITS_ALIEN (0x79A5): every ball that is in play, in
	// turn; one that has just hit an alien is sent away with its vertical
	// skewness turned over, and the sound is queued.
	void				CheckAnyBallHitsAlien();

	// CHECK_BALL_HITS_ALIEN (0x79FD): the ball against the three aliens.
	// True when no alien was touched, which is the original's carry.
	bool				CheckBallHitsAlien(int ball);

	// CHECK_DOH_HIT_AND_BOUNCE_BALL (0x967B): Doh's face is a box, and the
	// side the ball came from decides the bounce.
	void				CheckDohHitAndBounceBall(int ball);

	// DEACTIVE_ALL_BALLS (0x9710): the balls leave the field.
	void				DeactiveAllBalls();

private:
			State&		fState;
			Screen&		fScreen;
			Ball		fBall;
			Sound		fSound;
			BrickEffects	fEffects;
};


#endif	// ARKANOID_ALIENS_H
