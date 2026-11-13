/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef ARKANOID_BALL_H
#define ARKANOID_BALL_H


#include <stdint.h>

#include "State.h"


class State;


// The ball, ported routine by routine from the original. Each method is one
// routine of the disassembly; the original name and address are in the
// implementation.
//
// Ball positions are kept in the sprites' shadow copy, and the ball's speed,
// skewness and counters in its table, exactly where the original keeps them,
// so a caller can hand the same memory to both and compare.
class Ball {
public:
						Ball(State& state);

	// iy = ball table, ix = its sprite. index is 0, 1 or 2.
	void				UpdateSpeed(int index);
	void				CheckBouncesAndChangeSkewness();
	void				VerticalBounce(int index);
	void				HorizontalBounce(int index);
	void				InvertVerticalSkewness(int index);
	void				CheckGlueAndSkewness(int index, int sprite);
	void				OutBelow(int index, int sprite);

	// CHANGE_BALLS_SKEWNESS (0xAB38): the ball code uses it when the
	// bounces counter runs out, and the unbreakable brick's action calls it
	// too.
	void				ChangeBallsSkewness();

private:
	void				_AddSound(uint8_t sound);
	static uint16_t		_DivideHlByC(uint16_t value, uint8_t divisor);

	static uint16_t		_Table(int index);
	static uint16_t		_Sprite(int index);

			State&		fState;
};


#endif	// ARKANOID_BALL_H
