/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The ball moving.
//
// One step of the ball is not one pixel: the ball moves in a direction of its
// own choosing - its skewness - and takes from one to sixteen small moves
// before the frame is over, testing the bricks after each one. That is why a
// fast ball never ends up inside a brick: it is checked between every two of
// its small moves, not once per frame.
//
// The step length comes from a table per skewness, so a ball going straight up
// moves a whole row a frame while one going nearly sideways creeps.


#ifndef ARKANOID_BALL_MOTION_H
#define ARKANOID_BALL_MOTION_H


#include <stdint.h>

#include "Aliens.h"
#include "Ball.h"
#include "BrickCollision.h"
#include "Screen.h"
#include "State.h"


class BallMotion {
public:
						BallMotion(State& state, Screen& screen);

	// UPDATE_BALL_POSITION (0x99DF).
	void				UpdatePosition(int ball);

			BrickCollision& Collision() { return fCollision; }
			Aliens&		AlienState() { return fAliens; }

private:
			State&		fState;
			Ball		fBall;
			BrickCollision	fCollision;
			Aliens		fAliens;
};


#endif	// ARKANOID_BALL_MOTION_H
