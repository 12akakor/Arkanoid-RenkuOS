/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// What hitting a brick does.
//
// Each level has a table in the cartridge with one byte per brick, naming one
// of five actions, and a second table says where each level's table is.
// APPLY_BRICK_HIT_EFFECT reads the byte for the brick the ball hit and jumps to
// the action; the actions add points, count the level down, play a sound and
// start the animation of a hard brick that is not destroyed yet.
//
// All five actions are here. The capsule one hands over to Capsules, which
// spawns the capsule and then hits the brick like the others.


#ifndef ARKANOID_BRICK_EFFECTS_H
#define ARKANOID_BRICK_EFFECTS_H


#include <stdint.h>

#include "Ball.h"
#include "Brick.h"
#include "Capsules.h"
#include "Playfield.h"
#include "Random.h"
#include "Screen.h"
#include "Sound.h"
#include "State.h"


class BrickEffects {
public:
	// The five actions of TBL_BRICK_ACTIONS, in the cartridge's order.
	enum Action {
		kActionBrickHit				= 0,
		kActionBrickHitAndCapsule	= 1,
		kActionHardBrickHit			= 2,
		kActionUnbreakableBrickHit	= 3,
		kActionResetUnusedVars		= 4
	};

						BrickEffects(State& state, Screen& screen);

	// APPLY_BRICK_HIT_EFFECT (0xAA05): updates the ball's speed, then reads
	// the action the level gives the brick at BRICK_ROW and BRICK_COL. Row
	// and column are clamped to the table.
	uint8_t				ApplyHitEffect(int ball);

	// Runs the action.
	bool				PerformHitEffect(uint8_t action);

	// The source the capsule code draws from; a test can pin it.
	Random&				RandomSource() { return fRandom; }

	// The actions themselves, at their entry addresses.
	void				ActionBrickHit();			// 0xAAEF
	void				ActionHardBrickHit();		// 0xAAC7
	void				ActionUnbreakableBrickHit();	// 0xAAA4
	void				ResetUnusedVars();			// 0xAA9C

	// GIVE_BRICK_HIT_POINTS (0x9604) and the chain it starts.
	// DRAW_LIVES (0x71B9): the little Vauses at the top left, one per life
	// left. The round start calls it; the score chain does too.
	void				DrawLives();

	void				GiveBrickHitPoints();
	void				AddPoints(uint8_t points);	// 0x52A0

private:
	void				_HardBrickBounce(uint8_t kind);	// 0xAB06
	void				_UpdateHardBrickTable(uint8_t kind, uint16_t vram);
													// 0x97AF
	void				_DecBricksCheckLevelDone();	// 0xAB7A
	void				_AddBcd(uint16_t score, uint16_t points);	// 0x538A
	void				_CheckScoreLifeTarget();	// 0x5319
	uint16_t			_Read16(uint16_t address) const;

			State&		fState;
			Screen&		fScreen;
			Ball		fBall;
			Brick		fBrick;
			Playfield	fPlayfield;
			Sound		fSound;
			Random		fRandom;
			Capsules	fCapsules;
};


#endif	// ARKANOID_BRICK_EFFECTS_H
