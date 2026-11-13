/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The ball against the bricks.
//
// CHECK_BRICK_HIT_AND_BOUNCE_BALL works out which brick cell the ball is in and
// which it came from, and then has four cases - one per diagonal direction -
// each of which asks a small number of questions about the two cells and the
// ball's direction. Two of the helpers those cases use are here: the one that
// treats the ball running out of the brick field on the right, and the one that
// treats a ball that jumped fifteen columns, which the game calls the rare or
// impossible case. The third is the double impact, for the top and bottom rows.
//
// Both are reached with the brick row and column already worked out in
// CURR_BRICK_Y/X and PREV_BRICK_Y/X, and both answer through their return
// value: true when they have handled the hit.


#ifndef ARKANOID_BRICK_COLLISION_H
#define ARKANOID_BRICK_COLLISION_H


#include <stdint.h>

#include "Ball.h"
#include "Brick.h"
#include "BrickEffects.h"
#include "Collision.h"
#include "Screen.h"
#include "State.h"


class BrickCollision {
public:
						BrickCollision(State& state, Screen& screen);

	// CHECK_BALL_REACHES_RIGHT_BORDER (0xA29A): the ball is one cell past
	// the last column, so the brick it is tested against is the last one.
	bool				CheckBallReachesRightBorder(int ball);

	// CHECK_RARE_OR_IMPOSSIBLE_CASE (0xA2AD): the ball went from the first
	// column to the sixteenth in one step.
	bool				CheckRareOrImpossibleCase(int ball);

	// CHECK_VERTICAL_DOUBLE_IMPACT (0xA328): the ball's cell and the cell
	// it came from are the same column, so two bricks were met at once.
	void				CheckVerticalDoubleImpact(int ball);

	// CHECK_BRICK_HIT_AND_BOUNCE_BALL (0x9C4C): the ball against the brick
	// field, for one step of its movement.
	void				CheckBrickHitAndBounceBall(int ball);

private:
	bool				_EdgeCase(int ball);
	void				_BounceHorizontally(int ball, uint8_t row);
	void				_HitEffect(int ball);

	// One diagonal case: the cells are worked out the same way four times,
	// with the two offsets the direction needs, and then the same set of
	// answers follows from the row and column comparisons.
	void				_Octant(int ball, bool movingDown, bool movingRight);
	void				_FaceAndBounce(int ball, uint8_t row, uint8_t column,
							bool vertical, bool corner);
	void				_CornerPair(int ball, uint8_t currRow, uint8_t prevRow,
							uint8_t currColumn, uint8_t prevColumn);
	bool				_Exists(uint8_t row, uint8_t column);

			State&		fState;
			Screen&		fScreen;
			Ball		fBall;
			Brick		fBrick;
			Collision	fCollision;
			BrickEffects	fEffects;
};


#endif	// ARKANOID_BRICK_COLLISION_H
