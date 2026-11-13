/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "BallMotion.h"


namespace {

uint16_t
BallTable(int ball)
{
	return GameRam::kBallTables
		+ static_cast<uint16_t>(GameRam::kBallTableLength * ball);
}


uint16_t
BallSprite(int ball)
{
	return GameRam::kBallSprites
		+ static_cast<uint16_t>(GameRam::kSpriteLength * ball);
}


uint16_t
Read16(const State& state, uint16_t address)
{
	return static_cast<uint16_t>(state.Read(address)
		| (state.Read(address + 1) << 8));
}

}


BallMotion::BallMotion(State& state, Screen& screen)
	:
	fState(state),
	fBall(state),
	fCollision(state, screen),
	fAliens(state, screen)
{
}


// UPDATE_BALL_POSITION (0x99DF). Most calls only count a tick: the table for
// the ball's skewness says how many frames the ball waits between steps and how
// many small moves it then takes, so speed is a property of the direction.
void
BallMotion::UpdatePosition(int ball)
{
	const uint16_t table = BallTable(ball);
	const uint16_t sprite = BallSprite(ball);

	// The skewness names a direction; its sign only says which way round.
	const uint8_t skewness = fState.Read(table + GameRam::kSkewness);
	const bool negative = (skewness & 0x80) != 0;
	const uint8_t magnitude = negative
		? static_cast<uint8_t>(-skewness) : skewness;

	const uint16_t speedTable
		= Read16(fState, GameRam::kSpeedPerSkewnessPtr + 2 * magnitude);
	const uint16_t entry = static_cast<uint16_t>(speedTable
		+ 2 * fState.Read(table + GameRam::kSpeedPosition));

	fState.Write(table + GameRam::kSpeedMultiplier, fState.Read(entry));
	fState.Write(table + GameRam::kMoveTarget, fState.Read(entry + 1));

	const uint8_t counter
		= static_cast<uint8_t>(fState.Read(table + GameRam::kMoveCounter) + 1);
	fState.Write(table + GameRam::kMoveCounter, counter);
	if (counter < fState.Read(table + GameRam::kMoveTarget))
		return;
	fState.Write(table + GameRam::kMoveCounter, 0);

	// Skewness is never flat, so its magnitude is one more than the table's
	// index, and there are two tables: one while the ball goes up, one down.
	const uint16_t directions = negative
		? GameRam::kSkewnessNegToXY : GameRam::kSkewnessPosToXY;
	const uint16_t direction = static_cast<uint16_t>(directions
		+ 2 * (magnitude - 1));
	fState.Write(table + GameRam::kYSpeed, fState.Read(direction));
	fState.Write(table + GameRam::kXSpeed, fState.Read(direction + 1));

	// The small moves. The speeds are read again for each one, because a
	// bounce in the middle of the frame changes them.
	uint8_t steps
		= static_cast<uint8_t>(fState.Read(table + GameRam::kSpeedMultiplier) + 1);

	do {
		const uint8_t ySpeed = fState.Read(table + GameRam::kYSpeed);
		const uint8_t xSpeed = fState.Read(table + GameRam::kXSpeed);

		fState.Write(sprite + GameRam::kSpriteY,
			static_cast<uint8_t>(fState.Read(sprite + GameRam::kSpriteY)
				+ ySpeed));
		fState.Write(sprite + GameRam::kSpriteX,
			static_cast<uint8_t>(fState.Read(sprite + GameRam::kSpriteX)
				+ xSpeed));

		fState.Write(GameRam::kDohBeenHit, 0);

		if (fState.Read(GameRam::kLevel) == GameRam::kFinalLevel)
			fAliens.CheckDohHitAndBounceBall(ball);
		else
			fCollision.CheckBrickHitAndBounceBall(ball);

		// Doh's bounce ends the frame: the ball is not tested any further.
		if (fState.Read(GameRam::kDohBeenHit) != 0)
			return;
	} while (--steps != 0);

	fAliens.CheckAnyBallHitsAlien();
}
