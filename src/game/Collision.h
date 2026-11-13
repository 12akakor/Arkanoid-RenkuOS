/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef ARKANOID_COLLISION_H
#define ARKANOID_COLLISION_H


#include <stdint.h>

#include "State.h"


class State;


// Where exactly the ball crossed into a brick, in pixels.
//
// The original does not test the ball's centre: it walks an auxiliary
// trajectory, built from the ball's skewness, in sub-steps until it has
// crossed the brick's edge, and then works out where along the other axis it
// was at that moment. These two routines do that walk; the collision routine
// uses the answer to place the ball exactly on the brick's surface and to
// decide which face was hit.
//
// Both keep their working values in the game's own variables (TICKS_TO_HIT,
// BALL_X_SLOPE, BALL_Y_SLOPE, HIY_Y_EDGE_A/B, COMPUTED_HIT_X and its negative
// twin, BRICK_HIT_X_PIXEL/Y_PIXEL), because the routines that call them read
// those back. They are ports, not rewrites.
class Collision {
public:
						Collision(State& state);

	// COMPUTE_PRECISE_HIT_POINT (0xA3D1). BRICK_ROW and BRICK_COL say
	// which brick; the ball's position comes from PREV_X_PX/PREV_Y_PX,
	// which its caller has just stored. Returns the original's carry flag.
	bool				ComputePreciseHitPoint(int ball);

	// COMPUTE_WALL_ADJACENT_HIT_POINT (0xA591). The same walk for a brick
	// beside the wall. Returns the original's carry flag.
	bool				ComputeWallAdjacentHitPoint(int ball);

	// RESOLVE_CORNER_COLLISION (0xA670). The ball is at the joint of two
	// bricks: decide which one it met, place it there and say which face
	// won through the carry.
	bool				ResolveCornerCollision(int ball);

	// HANDLE_CORNER_CASE_VERTICAL (0xA800) and _HORIZONTAL (0xA901): the
	// same for a double impact, moving the ball onto the surface itself.
	void				HandleCornerCaseVertical(int ball, int sprite);
	void				HandleCornerCaseHorizontal(int ball, int sprite);

private:
	void				_ResetTemporaries();

	// Steps along the auxiliary slope until the coordinate passes the
	// target, counting sub-steps; returns the count.
	uint8_t				_Walk(uint8_t value, uint8_t slope, uint16_t target,
							bool positive);

			State&		fState;
};


#endif	// ARKANOID_COLLISION_H
