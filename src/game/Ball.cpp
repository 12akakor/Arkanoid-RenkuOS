/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#include "Ball.h"

#include <stdint.h>


namespace {

inline uint8_t
Negate(uint8_t value)
{
	return static_cast<uint8_t>(-static_cast<int>(value));
}

}	// namespace


Ball::Ball(State& state)
	:
	fState(state)
{
}


// UPDATE_BALL_SPEED (0x9AF0). Called after a brick is hit: the ball speeds
// up once it has stayed long enough at its current speed.
void
Ball::UpdateSpeed(int index)
{
	const uint16_t table = _Table(index);

	uint8_t counter = fState.Read(table + GameRam::kSpeedCounter) + 1;
	fState.Write(table + GameRam::kSpeedCounter, counter);

	const uint8_t speedPosition = fState.Read(table + GameRam::kSpeedPosition);
	const uint8_t target = fState.Read(GameRam::kBallSpeedTable
		+ speedPosition);
	if (target >= counter)
		return;

	fState.Write(table + GameRam::kSpeedCounter, 0);

	uint8_t next = speedPosition + 1;
	fState.Write(table + GameRam::kSpeedPosition, next);
	if (next == 16)
		fState.Write(table + GameRam::kSpeedPosition, 15);
}


// CHECK_BALL_BOUNCES_AND_CHANGE_SKEWNESS (0x99D1). Counts bounces against
// the borders; every 40, every ball takes the next skewness in the cycle, so
// the ball cannot stay in the same direction for ever.
void
Ball::CheckBouncesAndChangeSkewness()
{
	uint8_t bounces = fState.Read(GameRam::kBallBouncesCounter) + 1;
	fState.Write(GameRam::kBallBouncesCounter, bounces);
	if (bounces != 40)
		return;

	fState.Write(GameRam::kBallBouncesCounter, 0);
	ChangeBallsSkewness();
}


// BALL_VERTICAL_BOUNCE (0x9B5B). Inverts the vertical speed and mirrors the
// skewness about 9, so the ball leaves the surface at the same angle.
void
Ball::VerticalBounce(int index)
{
	const uint16_t table = _Table(index);

	const uint8_t ySpeed = fState.Read(table + GameRam::kYSpeed);
	fState.Write(table + GameRam::kYSpeed, Negate(ySpeed));

	const uint8_t skewness = fState.Read(table + GameRam::kSkewness);
	uint8_t value = skewness;
	if ((value & 0x80) != 0)
		value = Negate(value);
	value = static_cast<uint8_t>(value - 9);
	if ((skewness & 0x80) != 0)
		value = Negate(value);
	fState.Write(table + GameRam::kSkewness, value);
}


// BALL_HORIZONTAL_BOUNCE (0x9B80): the horizontal speed, then on into
// INVERT_BALL_VERTICAL_SKEWNESS below.
void
Ball::HorizontalBounce(int index)
{
	const uint16_t table = _Table(index);

	const uint8_t xSpeed = fState.Read(table + GameRam::kXSpeed);
	fState.Write(table + GameRam::kXSpeed, Negate(xSpeed));

	InvertVerticalSkewness(index);
}


// INVERT_BALL_VERTICAL_SKEWNESS (0x9B8A). Mirrors the skewness about 9
// keeping its sign, which is the horizontal counterpart of the bounce above.
void
Ball::InvertVerticalSkewness(int index)
{
	const uint16_t table = _Table(index);

	const uint8_t skewness = fState.Read(table + GameRam::kSkewness);
	uint8_t value = skewness;
	if ((value & 0x80) != 0)
		value = Negate(value);
	value = static_cast<uint8_t>(9 - value);
	if ((skewness & 0x80) != 0)
		value = Negate(value);
	fState.Write(table + GameRam::kSkewness, value);
}


// CHECK_UPDATE_BALL_GLUE_AND_SKEWNESS (0x9BA8). The ball has reached the
// Vaus: give it a skewness according to which seventh of the Vaus it hit, or
// glue it when Vaus is sticky.
void
Ball::CheckGlueAndSkewness(int index, int sprite)
{
	const uint16_t table = _Table(index);
	const uint16_t params = _Sprite(sprite);

	// Only a ball moving down can hit the Vaus.
	if ((fState.Read(table + GameRam::kYSpeed) & 0x80) != 0)
		return;

	const uint8_t ballY = fState.Read(params + GameRam::kSpriteY);
	if (ballY < 167 || ballY >= 173)
		return;

	const uint8_t ballX = fState.Read(params + GameRam::kSpriteX);
	const uint8_t vausX = fState.Read(GameRam::kVausX);

	if (static_cast<uint8_t>(vausX + 1) >= ballX)
		return;

	// The Vaus is seven pieces wide, ten when enlarged.
	uint8_t pieces = 7;
	uint8_t limit = 41;
	if (fState.Read(GameRam::kVausResizing) == GameRam::kActionStateEnlarging) {
		pieces = 10;
		limit = 57;
	}

	if (static_cast<uint8_t>(vausX + limit) < ballX)
		return;

	fState.Write(params + GameRam::kSpriteY, 169);

	if (fState.Read(GameRam::kGluingStatus) == GameRam::kGluingStateSticky) {
		fState.Write(table + GameRam::kGlueCounter, 240);
		fState.Write(table + GameRam::kGlue, 1);
		fState.Write(table + GameRam::kVausHitX,
			static_cast<uint8_t>(ballX - vausX));
		_AddSound(GameRam::kSoundGluedBall);
	} else
		_AddSound(GameRam::kSoundBallOnVaus);

	// Upwards now.
	const uint8_t ySpeed = fState.Read(table + GameRam::kYSpeed);
	fState.Write(table + GameRam::kYSpeed, Negate(ySpeed));

	// Which piece of the Vaus was hit decides the angle.
	const uint16_t piece = _DivideHlByC(ballX - vausX, pieces);
	const uint8_t skewness = fState.Read(GameRam::kVausSkewnessTable + piece);
	fState.Write(table + GameRam::kSkewness, skewness);
}


// BALL_OUT_BELOW (0x9B2A). The ball has left the field: clear it, and either
// give up an extra ball, or explode the Vaus.
void
Ball::OutBelow(int index, int sprite)
{
	const uint16_t table = _Table(index);
	const uint16_t params = _Sprite(sprite);

	for (int i = 0; i < GameRam::kBallTableLength; i++)
		fState.Write(table + i, 0);

	fState.Write(params + GameRam::kSpriteY, 192);
	fState.Write(params + GameRam::kSpriteColour, 0);

	if (fState.Read(GameRam::kExtraBalls) != 0) {
		fState.Write(GameRam::kExtraBalls,
			fState.Read(GameRam::kExtraBalls) - 1);
		return;
	}

	// A Vaus on its way through the portal has already escaped.
	if (fState.Read(GameRam::kVausActionState) == GameRam::kActionStatePortal)
		return;

	fState.Write(GameRam::kVausActionState, GameRam::kActionStateExploding);
	_AddSound(GameRam::kSoundVausDestroyed);
}


// CHANGE_BALLS_SKEWNESS (0xAB38). Also called between levels. The skewness
// runs 1..7 for one direction and 0xF9..0xFF for the other; the table holds
// the same cycle for both.
void
Ball::ChangeBallsSkewness()
{
	for (int index = 0; index < GameRam::kBallCount; index++) {
		const uint16_t table = _Table(index);
		if (fState.Read(table + GameRam::kActive) != 1)
			continue;

		uint8_t skewness = fState.Read(table + GameRam::kSkewness);
		if ((skewness & 0x80) != 0)
			skewness = static_cast<uint8_t>(Negate(skewness) + 8);

		const uint8_t next = fState.Read(GameRam::kCyclicSkewnessTable
			+ skewness - 1);
		fState.Write(table + GameRam::kSkewness, next);
	}
}


// ADD_SOUND (0x5BEF). Queues a sound for the sound driver; ignored while the
// lasers are firing, and the queue holds at most seven.
void
Ball::_AddSound(uint8_t sound)
{
	if (fState.Read(GameRam::kLasersFiring) != 0)
		return;

	uint8_t count = fState.Read(GameRam::kSoundsCount);
	fState.Write(GameRam::kSoundsBuffer + count, sound);

	count++;
	if (count == 8)
		count = 7;
	fState.Write(GameRam::kSoundsCount, count);
}


// DIVIDE_HL_BY_C (0xB39A), the original's own 16 by 8 division. The quotient
// goes in the returned value, the remainder in remainder when wanted; the
// caller here uses only the low byte of the quotient.
uint16_t
Ball::_DivideHlByC(uint16_t value, uint8_t divisor)
{
	uint8_t remainder = 0;
	for (int bit = 0; bit < 16; bit++) {
		const uint8_t shiftedOut = static_cast<uint8_t>(value >> 15);
		value = static_cast<uint16_t>(value << 1);

		const uint8_t carry = static_cast<uint8_t>(remainder >> 7);
		remainder = static_cast<uint8_t>((remainder << 1) | shiftedOut);

		if (carry != 0 || remainder >= divisor) {
			remainder = static_cast<uint8_t>(remainder - divisor);
			value++;
		}
	}
	return value;
}


uint16_t
Ball::_Table(int index)
{
	return GameRam::kBallTables
		+ static_cast<uint16_t>(GameRam::kBallTableLength * index);
}


uint16_t
Ball::_Sprite(int index)
{
	return GameRam::kBallSprites
		+ static_cast<uint16_t>(GameRam::kSpriteLength * index);
}
