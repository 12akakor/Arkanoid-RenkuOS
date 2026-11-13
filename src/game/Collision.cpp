/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#include "Collision.h"

#include <stdint.h>


namespace {

inline uint8_t
Negate(uint8_t value)
{
	return static_cast<uint8_t>(-static_cast<int>(value));
}


uint16_t
BallTable(int ball)
{
	return GameRam::kBallTables
		+ static_cast<uint16_t>(GameRam::kBallTableLength * ball);
}


}	// namespace


Collision::Collision(State& state)
	:
	fState(state)
{
}


// Both routines start by clearing their scratch: TICKS_TO_HIT and the two
// slopes (`ldir` of two bytes from a zero).
void
Collision::_ResetTemporaries()
{
	fState.Write(GameRam::kTicksToHit, 0);
	fState.Write(GameRam::kBallXSlope, 0);
	fState.Write(GameRam::kBallYSlope, 0);
}


namespace {

// The slopes the walk uses, from TBL_SPEED_FROM_SKEWNESS at 2*(|skewness|-1),
// negated when the ball is on its way down.
void
SetSlopes(State& state, int ball)
{
	const uint16_t table = BallTable(ball);
	const uint8_t ySpeed = state.Read(table + GameRam::kYSpeed);

	uint8_t skewness = state.Read(table + GameRam::kSkewness);
	if ((skewness & 0x80) != 0)
		skewness = Negate(skewness);
	skewness = static_cast<uint8_t>(skewness - 1);

	const uint16_t base = static_cast<uint16_t>(GameRam::kSpeedFromSkewness
		+ 2 * skewness);
	uint8_t x = state.Read(base);
	uint8_t y = state.Read(base + 1);

	if ((ySpeed & 0x80) == 0) {
		// Going down: both slopes are negated.
		x = Negate(x);
		y = Negate(y);
	}

	state.Write(GameRam::kBallXSlope, x);
	state.Write(GameRam::kBallYSlope, y);
}


// `ld a,slope; neg; loop: add a,slope; djnz loop`, which is (count - 1) times
// the slope. A count of zero loops 256 times, as DJNZ does; the walk never
// produces one, but the arithmetic is kept as it is.
uint8_t
SlopeTimes(uint8_t slope, uint8_t count)
{
	uint8_t value = Negate(slope);
	const unsigned iterations = count == 0 ? 256 : count;
	for (unsigned i = 0; i < iterations; i++)
		value = static_cast<uint8_t>(value + slope);
	return value;
}

}	// namespace


// COMPUTE_PRECISE_HIT_POINT (0xA3D1). Computes where the ball crossed into
// the brick: first the X crossing decides the sub-step, then Y is
// reconstructed at that sub-step, and the result is written to
// BRICK_HIT_X_PIXEL and BRICK_HIT_Y_PIXEL. The carry says which of the two
// faces the caller should treat as hit.
bool
Collision::ComputePreciseHitPoint(int ball)
{
	const uint16_t table = BallTable(ball);
	const uint8_t ySpeed = fState.Read(table + GameRam::kYSpeed);
	const uint8_t xSpeed = fState.Read(table + GameRam::kXSpeed);
	const bool movingDown = (ySpeed & 0x80) == 0;
	const bool movingRight = (xSpeed & 0x80) == 0;

	_ResetTemporaries();

	// The brick's X band: 16*column, plus the ball's own half-width on
	// the leading side.
	uint8_t a = static_cast<uint8_t>(fState.Read(GameRam::kBrickCol) << 4);
	a = static_cast<uint8_t>(a + (movingRight ? 12 : 16));
	fState.Write(GameRam::kComputedHitX, a);
	fState.Write(GameRam::kComputedHitXNeg, static_cast<uint8_t>(a + 15));

	SetSlopes(fState, ball);

	// Step along X until it crosses the brick's edge.
	const uint16_t xTarget = movingRight ? GameRam::kComputedHitX
		: GameRam::kComputedHitXNeg;
	if (movingRight)
		fState.Write(xTarget, static_cast<uint8_t>(fState.Read(xTarget) + 1));

	uint8_t x = fState.Read(GameRam::kPrevXPixel);
	const uint8_t xSlope = fState.Read(GameRam::kBallXSlope);
	uint8_t ticks = 0;
	if (movingRight) {
		for (;;) {
			ticks++;
			x = static_cast<uint8_t>(x + xSlope);
			if (x >= fState.Read(xTarget))
				break;
		}
		fState.Write(xTarget, static_cast<uint8_t>(fState.Read(xTarget) - 1));
	} else {
		for (;;) {
			ticks++;
			x = static_cast<uint8_t>(x + xSlope);
			if (x < fState.Read(xTarget))
				break;
		}
	}
	fState.Write(GameRam::kTicksToHit, ticks);

	// Y at that sub-step.
	uint8_t y = static_cast<uint8_t>(fState.Read(GameRam::kPrevYPixel)
		+ SlopeTimes(fState.Read(GameRam::kBallYSlope), ticks));

	// The original clamps the estimate to the brick's own vertical band.
	const uint8_t row = fState.Read(GameRam::kBrickRow);
	if (movingDown) {
		a = static_cast<uint8_t>(row * 8 + 18);
		if (a < y)
			a = static_cast<uint8_t>(a + 8);
		else
			a = static_cast<uint8_t>(a + 1);
	} else {
		a = static_cast<uint8_t>(row * 8 + 31);
		if (a >= y)
			a = static_cast<uint8_t>(a - 8 + 1);
		else
			a = static_cast<uint8_t>(a);
	}
	fState.Write(GameRam::kVausX2, a);

	fState.Write(GameRam::kBrickHitXPixel, movingRight
		? fState.Read(GameRam::kComputedHitX)
		: fState.Read(GameRam::kComputedHitXNeg));

	// Second walk, this time along Y, to find the other coordinate.
	_ResetTemporaries();

	a = static_cast<uint8_t>(row * 8);
	const uint8_t edge = movingDown ? 19 : 24;
	a = static_cast<uint8_t>(a + edge);
	fState.Write(GameRam::kHiyYEdgeA, a);
	fState.Write(GameRam::kHiyYEdgeB, static_cast<uint8_t>(a + 7));

	SetSlopes(fState, ball);

	const uint16_t yTarget = movingDown ? GameRam::kHiyYEdgeA
		: GameRam::kHiyYEdgeB;
	if (movingDown)
		fState.Write(yTarget, static_cast<uint8_t>(fState.Read(yTarget) + 1));

	uint8_t value = fState.Read(GameRam::kPrevYPixel);
	const uint8_t ySlope = fState.Read(GameRam::kBallYSlope);
	ticks = 0;
	if (movingDown) {
		for (;;) {
			ticks++;
			value = static_cast<uint8_t>(value + ySlope);
			if (value >= fState.Read(yTarget))
				break;
		}
		fState.Write(yTarget, static_cast<uint8_t>(fState.Read(yTarget) - 1));
	} else {
		for (;;) {
			ticks++;
			value = static_cast<uint8_t>(value + ySlope);
			if (value < fState.Read(yTarget))
				break;
		}
	}
	fState.Write(GameRam::kTicksToHit, ticks);

	// The original calls this the Y of the hit, though it comes from X:
	// the walk was along Y, so the coordinate to reconstruct is X.
	value = static_cast<uint8_t>(fState.Read(GameRam::kPrevXPixel)
		+ SlopeTimes(fState.Read(GameRam::kBallXSlope), ticks));
	fState.Write(GameRam::kBrickHitYPixel, value);

	if (!movingRight)
		return value > 15;
	return value <= 188;
}


// The walk the collision routines share: step `value` by `slope` until it
// passes the target, counting the sub-steps. Going down the boundary is
// crossed when the value reaches it, going up when it drops below it, and the
// target is nudged by one first in the down case and put back afterwards.
uint8_t
Collision::_Walk(uint8_t value, uint8_t slope, uint16_t target,
	bool positive)
{
	uint8_t ticks = 0;
	if (positive) {
		fState.Write(target, static_cast<uint8_t>(fState.Read(target) + 1));
		for (;;) {
			ticks++;
			value = static_cast<uint8_t>(value + slope);
			if (value >= fState.Read(target))
				break;
		}
		fState.Write(target, static_cast<uint8_t>(fState.Read(target) - 1));
	} else {
		for (;;) {
			ticks++;
			value = static_cast<uint8_t>(value + slope);
			if (value < fState.Read(target))
				break;
		}
	}
	fState.Write(GameRam::kTicksToHit, ticks);
	return ticks;
}


// COMPUTE_WALL_ADJACENT_HIT_POINT (0xA591). The same walk, for a brick at the
// wall, and it leaves the answer in BRICK_HIT_X_PIXEL/Y_PIXEL.
bool
Collision::ComputeWallAdjacentHitPoint(int ball)
{
	const uint16_t table = BallTable(ball);
	const uint8_t ySpeed = fState.Read(table + GameRam::kYSpeed);
	const uint8_t xSpeed = fState.Read(table + GameRam::kXSpeed);
	const bool movingDown = (ySpeed & 0x80) == 0;

	_ResetTemporaries();

	const uint8_t row = fState.Read(GameRam::kBrickRow);
	uint8_t a = static_cast<uint8_t>(row * 8 + (movingDown ? 19 : 24));
	fState.Write(GameRam::kHiyYEdgeA, a);
	fState.Write(GameRam::kHiyYEdgeB, static_cast<uint8_t>(a + 7));

	SetSlopes(fState, ball);

	const uint16_t yTarget = movingDown ? GameRam::kHiyYEdgeA
		: GameRam::kHiyYEdgeB;
	if (movingDown)
		fState.Write(yTarget, static_cast<uint8_t>(fState.Read(yTarget) + 1));

	uint8_t value = fState.Read(GameRam::kPrevYPixel);
	const uint8_t ySlope = fState.Read(GameRam::kBallYSlope);
	uint8_t ticks = 0;
	if (movingDown) {
		for (;;) {
			ticks++;
			value = static_cast<uint8_t>(value + ySlope);
			if (value >= fState.Read(yTarget))
				break;
		}
		fState.Write(yTarget, static_cast<uint8_t>(fState.Read(yTarget) - 1));
	} else {
		for (;;) {
			ticks++;
			value = static_cast<uint8_t>(value + ySlope);
			if (value < fState.Read(yTarget))
				break;
		}
	}
	fState.Write(GameRam::kTicksToHit, ticks);

	uint8_t x = static_cast<uint8_t>(fState.Read(GameRam::kPrevXPixel)
		+ SlopeTimes(fState.Read(GameRam::kBallXSlope), ticks));

	bool carry;
	if ((xSpeed & 0x80) != 0) {
		carry = x > 15;
		if (!carry)
			x = 16;
	} else {
		carry = x <= 188;
		if (!carry)
			x = 188;
	}

	fState.Write(GameRam::kBrickHitXPixel, x);
	fState.Write(GameRam::kBrickHitYPixel, fState.Read(yTarget));
	return carry;
}


namespace {

uint16_t
SpriteParams(int sprite)
{
	return GameRam::kBallSprites
		+ static_cast<uint16_t>(GameRam::kSpriteLength * sprite);
}

}	// namespace


// RESOLVE_CORNER_COLLISION (0xA670). The ball reached the joint between two
// bricks. The same walk decides where it crossed the row's boundary; then the
// X it had at that moment is compared with the brick's band, and the answer
// says whether the vertical or the horizontal face was met.
bool
Collision::ResolveCornerCollision(int ball)
{
	const uint16_t table = BallTable(ball);
	const uint8_t ySpeed = fState.Read(table + GameRam::kYSpeed);
	const uint8_t xSpeed = fState.Read(table + GameRam::kXSpeed);
	const bool movingDown = (ySpeed & 0x80) == 0;
	const bool movingRight = (xSpeed & 0x80) == 0;

	_ResetTemporaries();

	const uint8_t row = fState.Read(GameRam::kBrickRow);
	uint8_t a = static_cast<uint8_t>(row * 8 + (movingDown ? 19 : 24));
	fState.Write(GameRam::kHiyYEdgeA, a);
	fState.Write(GameRam::kHiyYEdgeB, static_cast<uint8_t>(a + 7));

	SetSlopes(fState, ball);

	const uint8_t ticks = _Walk(fState.Read(GameRam::kPrevYPixel),
		fState.Read(GameRam::kBallYSlope),
		movingDown ? GameRam::kHiyYEdgeA : GameRam::kHiyYEdgeB, movingDown);

	uint8_t b = static_cast<uint8_t>(fState.Read(GameRam::kPrevXPixel)
		+ SlopeTimes(fState.Read(GameRam::kBallXSlope), ticks));

	const uint8_t column = static_cast<uint8_t>(
		fState.Read(GameRam::kBrickCol) << 4);
	bool carry;

	// Two variants of the same three comparisons: the one going right
	// treats the first hit as the horizontal face, the one going left the
	// opposite way round.
	a = static_cast<uint8_t>(column + (movingRight ? 12 : 16));
	if (a >= b) {
		// The first edge of the band was missed on the near side.
		b = a;
		carry = movingRight;
	} else {
		a = static_cast<uint8_t>(a + 31);
		if (a >= b) {
			// Inside the band: which of its two corners decides.
			a = static_cast<uint8_t>(a - 16);
			carry = a >= b;
		} else {
			b = a;
			carry = !movingRight;
		}
	}

	fState.Write(GameRam::kBrickHitXPixel, b);
	fState.Write(GameRam::kBrickHitYPixel,
		fState.Read(movingDown ? GameRam::kHiyYEdgeA : GameRam::kHiyYEdgeB));
	return carry;
}


// HANDLE_CORNER_CASE_VERTICAL (0xA800). The double impact was resolved
// vertically: put the ball on the brick's surface, at the X it had when it
// crossed the row's edge.
void
Collision::HandleCornerCaseVertical(int ball, int sprite)
{
	const uint16_t table = BallTable(ball);
	const uint8_t ySpeed = fState.Read(table + GameRam::kYSpeed);
	const uint8_t xSpeed = fState.Read(table + GameRam::kXSpeed);
	const bool movingDown = (ySpeed & 0x80) == 0;
	const bool movingRight = (xSpeed & 0x80) == 0;

	_ResetTemporaries();

	const uint8_t row = fState.Read(GameRam::kBrickRow);
	uint8_t a = static_cast<uint8_t>(row * 8 + (movingDown ? 19 : 24));
	fState.Write(GameRam::kHiyYEdgeA, a);
	fState.Write(GameRam::kHiyYEdgeB, static_cast<uint8_t>(a + 7));

	SetSlopes(fState, ball);

	const uint8_t ticks = _Walk(fState.Read(GameRam::kPrevYPixel),
		fState.Read(GameRam::kBallYSlope),
		movingDown ? GameRam::kHiyYEdgeA : GameRam::kHiyYEdgeB, movingDown);

	uint8_t b = static_cast<uint8_t>(fState.Read(GameRam::kPrevXPixel)
		+ SlopeTimes(fState.Read(GameRam::kBallXSlope), ticks));

	// The X is clamped to the brick's band: 17 when the ball goes left,
	// 12 when it goes right, and 15 further if that first edge is missed.
	a = static_cast<uint8_t>((fState.Read(GameRam::kBrickCol) << 4)
		+ (movingRight ? 12 : 17));
	if (a >= b)
		b = a;
	else {
		a = static_cast<uint8_t>(a + 15);
		if (a < b)
			b = a;
	}

	const uint16_t params = SpriteParams(sprite);
	fState.Write(params + GameRam::kSpriteX, b);
	fState.Write(params + GameRam::kSpriteY,
		fState.Read(movingDown ? GameRam::kHiyYEdgeA : GameRam::kHiyYEdgeB));
}


// HANDLE_CORNER_CASE_HORIZONTAL (0xA901). The other resolution: the ball is
// placed on the brick's side, at the Y it had when it crossed the brick's
// vertical band.
void
Collision::HandleCornerCaseHorizontal(int ball, int sprite)
{
	const uint16_t table = BallTable(ball);
	const uint8_t ySpeed = fState.Read(table + GameRam::kYSpeed);
	const uint8_t xSpeed = fState.Read(table + GameRam::kXSpeed);
	const bool movingDown = (ySpeed & 0x80) == 0;
	const bool movingRight = (xSpeed & 0x80) == 0;

	_ResetTemporaries();

	uint8_t a = static_cast<uint8_t>((fState.Read(GameRam::kBrickCol) << 4)
		+ (movingRight ? 12 : 16));
	fState.Write(GameRam::kComputedHitX, a);
	fState.Write(GameRam::kComputedHitXNeg, static_cast<uint8_t>(a + 15));

	SetSlopes(fState, ball);

	const uint8_t ticks = _Walk(fState.Read(GameRam::kPrevXPixel),
		fState.Read(GameRam::kBallXSlope),
		movingRight ? GameRam::kComputedHitX : GameRam::kComputedHitXNeg,
		movingRight);

	uint8_t b = static_cast<uint8_t>(fState.Read(GameRam::kPrevYPixel)
		+ SlopeTimes(fState.Read(GameRam::kBallYSlope), ticks));

	const uint8_t row = fState.Read(GameRam::kBrickRow);
	a = static_cast<uint8_t>(row * 8 + (movingDown ? 20 : 31));
	if (movingDown) {
		if (a < b)
			a = static_cast<uint8_t>(a + 8);
		b = a;
	} else {
		if (a >= b)
			b = static_cast<uint8_t>(a - 8 + 1);
		else
			b = static_cast<uint8_t>(a + 1);
	}

	const uint16_t params = SpriteParams(sprite);
	fState.Write(params + GameRam::kSpriteY, b);
	fState.Write(params + GameRam::kSpriteX, movingRight
		? fState.Read(GameRam::kComputedHitX)
		: fState.Read(GameRam::kComputedHitXNeg));
}
