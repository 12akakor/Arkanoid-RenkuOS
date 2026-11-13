/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "BrickCollision.h"


namespace {

uint16_t
BallTable(int ball)
{
	return GameRam::kBallTables
		+ static_cast<uint16_t>(GameRam::kBallTableLength * ball);
}


uint16_t
Sprite(int ball)
{
	return GameRam::kBallSprites
		+ static_cast<uint16_t>(GameRam::kSpriteLength * ball);
}

}


BrickCollision::BrickCollision(State& state, Screen& screen)
	:
	fState(state),
	fScreen(screen),
	fBall(state),
	fBrick(state),
	fCollision(state),
	fEffects(state, screen)
{
}


// CHECK_BALL_REACHES_RIGHT_BORDER (0xA29A): only a ball that has just left the
// last column while being in the first one past it takes this path.
bool
BrickCollision::CheckBallReachesRightBorder(int ball)
{
	if (fState.Read(GameRam::kCurrBrickX) != 11)
		return false;
	if (fState.Read(GameRam::kPrevBrickX) != 10)
		return false;

	return _EdgeCase(ball);
}


// CHECK_RARE_OR_IMPOSSIBLE_CASE (0xA2AD): the same body, entered when the ball
// appears to have crossed the whole field in one step.
bool
BrickCollision::CheckRareOrImpossibleCase(int ball)
{
	if (fState.Read(GameRam::kCurrBrickX) != 15)
		return false;
	if (fState.Read(GameRam::kPrevBrickX) != 0)
		return false;

	return _EdgeCase(ball);
}


// The body both share (0xA2BD). With the ball in the same row as before, the
// brick under it is the one it hit. One row further along and it is the far
// corner of the brick it is leaving, which the wall-adjacent hit point resolves;
// when that produces no brick, the same-row answer is used after all.
bool
BrickCollision::_EdgeCase(int ball)
{
	const uint16_t table = BallTable(ball);
	const uint8_t prevY = fState.Read(GameRam::kPrevBrickY);
	const uint8_t currY = fState.Read(GameRam::kCurrBrickY);

	if (currY == prevY) {
		_BounceHorizontally(ball, prevY);
		return true;
	}

	const bool movingDown = (fState.Read(table + GameRam::kYSpeed) & 0x80) == 0;
	const uint8_t nextRow = static_cast<uint8_t>(
		prevY + (movingDown ? 1 : -1));
	if (currY != nextRow)
		return true;

	fState.Write(GameRam::kBrickRow, currY);
	if (!fCollision.ComputeWallAdjacentHitPoint(ball)) {
		_BounceHorizontally(ball, prevY);
		return true;
	}

	const uint8_t column = fState.Read(GameRam::kPrevBrickX);
	fState.Write(GameRam::kBrickCol, column);
	if (!fBrick.ExistsAt(currY, column)) {
		fState.Write(GameRam::kBrickRow, currY);
		fCollision.ComputePreciseHitPoint(ball);
		fBall.HorizontalBounce(ball);
		return true;
	}

	const uint16_t sprite = Sprite(ball);
	fState.Write(sprite + GameRam::kSpriteY,
		fState.Read(GameRam::kBrickHitYPixel));
	fState.Write(sprite + GameRam::kSpriteX,
		fState.Read(GameRam::kBrickHitXPixel));
	fBall.VerticalBounce(ball);
	_HitEffect(ball);
	return true;
}


// CHECK_VERTICAL_DOUBLE_IMPACT (0xA328). If the brick the hit-point code was
// aimed at is in the column the ball is in, that brick settles it. Otherwise
// the corner resolver says which face was met: the vertical one takes the brick
// the ball came from, the horizontal one the brick it is in, and if neither has
// a brick the other is tried.
void
BrickCollision::CheckVerticalDoubleImpact(int ball)
{
	const uint16_t table = BallTable(ball);
	const uint8_t row = fState.Read(GameRam::kCurrBrickY);
	const uint8_t column = fState.Read(GameRam::kCurrBrickX);
	const uint8_t prevColumn = fState.Read(GameRam::kPrevBrickX);

	if (fState.Read(GameRam::kBrickCol) == column) {
		fState.Write(GameRam::kBrickRow, row);
		fState.Write(GameRam::kBrickCol, column);
		if (!fBrick.ExistsAt(row, column))
			return;

		fCollision.HandleCornerCaseVertical(ball, ball);
		fBall.VerticalBounce(ball);
		_HitEffect(ball);
		return;
	}

	fState.Write(GameRam::kBrickRow, row);
	fState.Write(GameRam::kBrickCol,
		(fState.Read(table + GameRam::kXSpeed) & 0x80) != 0
			? column : prevColumn);

	if (fCollision.ResolveCornerCollision(ball)) {
		fState.Write(GameRam::kBrickRow, row);
		fState.Write(GameRam::kBrickCol, prevColumn);
		if (fBrick.ExistsAt(row, prevColumn)) {
			fCollision.HandleCornerCaseVertical(ball, ball);
			fBall.VerticalBounce(ball);
			_HitEffect(ball);
			return;
		}

		// The brick the ball came from is gone: try the one it is in.
		fState.Write(GameRam::kBrickRow, row);
		fState.Write(GameRam::kBrickCol, column);
		if (!fBrick.ExistsAt(row, column))
			return;

		fCollision.HandleCornerCaseHorizontal(ball, ball);
		fBall.HorizontalBounce(ball);
		_HitEffect(ball);
		return;
	}

	fState.Write(GameRam::kBrickRow, row);
	fState.Write(GameRam::kBrickCol, column);
	if (!fBrick.ExistsAt(row, column))
		return;

	fCollision.HandleCornerCaseVertical(ball, ball);
	fBall.VerticalBounce(ball);
	_HitEffect(ball);
}


// CHECK_BRICK_HIT_AND_BOUNCE_BALL (0x9C4C). The level's last one has no bricks
// left to hit. Otherwise the two speed signs pick one of four diagonal cases.
void
BrickCollision::CheckBrickHitAndBounceBall(int ball)
{
	if (fState.Read(GameRam::kLevel) >= GameRam::kFinalLevel)
		return;

	const uint16_t table = BallTable(ball);
	const bool movingDown
		= (fState.Read(table + GameRam::kYSpeed) & 0x80) == 0;
	const bool movingRight
		= (fState.Read(table + GameRam::kXSpeed) & 0x80) == 0;

	_Octant(ball, movingDown, movingRight);
}


// The two offsets say where in a brick the ball's sprite sits: eight pixels a
// brick row, sixteen a column, with the origin of the field at (24, 12) or
// (19, 17) depending on which way the ball is going. That is why the same cell
// is computed four ways.
void
BrickCollision::_Octant(int ball, bool movingDown, bool movingRight)
{
	const uint16_t table = BallTable(ball);
	const uint16_t sprite = Sprite(ball);

	const uint8_t yOffset = movingDown ? 19 : 24;
	const uint8_t xOffset = movingRight ? 12 : 17;

	const uint8_t spriteY = fState.Read(sprite + GameRam::kSpriteY);
	const uint8_t spriteX = fState.Read(sprite + GameRam::kSpriteX);
	const uint8_t ySpeed = fState.Read(table + GameRam::kYSpeed);
	const uint8_t xSpeed = fState.Read(table + GameRam::kXSpeed);

	// The Z80 subtracts and shifts in eight bits, so the intermediate has to
	// wrap: a sprite above the field's origin gives 0xF9, not -7.
	const uint8_t currRow = static_cast<uint8_t>(
		static_cast<uint8_t>(spriteY - yOffset) >> 3);
	if (currRow >= 12)
		return;
	fState.Write(GameRam::kCurrBrickY, currRow);

	const uint8_t currColumn = static_cast<uint8_t>(
		static_cast<uint8_t>(spriteX - xOffset) >> 4);
	fState.Write(GameRam::kCurrBrickX, currColumn);

	const uint8_t prevY = static_cast<uint8_t>(spriteY - ySpeed);
	fState.Write(GameRam::kPrevYPixel, prevY);

	const uint8_t prevRow = static_cast<uint8_t>(
		static_cast<uint8_t>(prevY - yOffset) >> 3);
	// A ball going up can only have come from one row past the field (row 12);
	// one going down has its previous row above it, so the only value out of
	// range is 31, which is the double impact that is looked for below.
	if (!movingDown && prevRow >= 13)
		return;
	fState.Write(GameRam::kPrevBrickY, prevRow);

	const uint8_t prevX = static_cast<uint8_t>(spriteX - xSpeed);
	fState.Write(GameRam::kPrevXPixel, prevX);

	const uint8_t prevColumn = static_cast<uint8_t>(
		static_cast<uint8_t>(prevX - xOffset) >> 4);
	if (prevColumn >= 11)
		return;
	fState.Write(GameRam::kPrevBrickX, prevColumn);

	// Two of the four cases test a ball that has left the field on the right,
	// the other two the one that jumped the whole width.
	const bool handled = movingRight
		? CheckBallReachesRightBorder(ball)
		: CheckRareOrImpossibleCase(ball);
	if (handled)
		return;

	if (fState.Read(GameRam::kCurrBrickX) >= 11)
		return;

	// The double impact: the ball is in the first or last row with the cell it
	// came from exactly one row beyond the field.
	const uint8_t lastRow = movingDown ? 0 : 11;
	const uint8_t beyondRow = movingDown ? 31 : 12;
	if (currRow == lastRow && prevRow == beyondRow) {
		CheckVerticalDoubleImpact(ball);
		return;
	}

	if (prevRow >= 12)
		return;

	// Same row, or one row on.
	const uint8_t rowStep = static_cast<uint8_t>(
		prevRow + (movingDown ? 1 : 0xFF));
	if (currRow != prevRow && currRow != rowStep)
		return;

	// Same column, or one column across.
	const uint8_t columnStep = static_cast<uint8_t>(
		prevColumn + (movingRight ? 1 : 0xFF));
	if (currColumn == prevColumn) {
		if (currRow == prevRow)
			// The face the ball is under: it bounces up or down.
			_FaceAndBounce(ball, prevRow, currColumn, true, false);
		else
			// The face it is leaving: the corner decides.
			_FaceAndBounce(ball, currRow, prevColumn, true, true);
		return;
	}

	if (currColumn != columnStep)
		return;

	if (currRow == prevRow)
		_FaceAndBounce(ball, prevRow, currColumn, false, true);
	else
		_CornerPair(ball, currRow, prevRow, currColumn, prevColumn);
}


bool
BrickCollision::_Exists(uint8_t row, uint8_t column)
{
	fState.Write(GameRam::kBrickRow, row);
	fState.Write(GameRam::kBrickCol, column);
	return fBrick.ExistsAt(row, column);
}


void
BrickCollision::_FaceAndBounce(int ball, uint8_t row, uint8_t column,
	bool vertical, bool corner)
{
	if (!_Exists(row, column))
		return;

	if (vertical) {
		if (corner)
			fCollision.HandleCornerCaseVertical(ball, ball);
		fBall.VerticalBounce(ball);
	} else {
		if (corner)
			fCollision.HandleCornerCaseHorizontal(ball, ball);
		fBall.HorizontalBounce(ball);
	}

	_HitEffect(ball);
}


// The two bricks a corner can be: the resolver says which face the ball met, and
// when that brick is not there the other one is tried.
void
BrickCollision::_CornerPair(int ball, uint8_t currRow, uint8_t prevRow,
	uint8_t currColumn, uint8_t prevColumn)
{
	fState.Write(GameRam::kBrickRow, currRow);
	fState.Write(GameRam::kBrickCol, prevColumn);
	const bool resolved = fCollision.ResolveCornerCollision(ball);

	if (resolved) {
		if (!fBrick.ExistsAt(currRow, prevColumn)) {
			_FaceAndBounce(ball, currRow, currColumn, false, true);
			return;
		}

		const uint16_t sprite = Sprite(ball);
		fState.Write(sprite + GameRam::kSpriteY,
			fState.Read(GameRam::kBrickHitYPixel));
		fState.Write(sprite + GameRam::kSpriteX,
			fState.Read(GameRam::kBrickHitXPixel));
		fBall.VerticalBounce(ball);
		_HitEffect(ball);
		return;
	}

	if (_Exists(prevRow, currColumn)) {
		_FaceAndBounce(ball, prevRow, currColumn, false, true);
		return;
	}

	_FaceAndBounce(ball, currRow, currColumn, true, true);
}


// The same-row answer: the hit point for that row, and a horizontal bounce.
void
BrickCollision::_BounceHorizontally(int ball, uint8_t row)
{
	fState.Write(GameRam::kBrickRow, row);
	fCollision.ComputePreciseHitPoint(ball);
	fBall.HorizontalBounce(ball);
}


void
BrickCollision::_HitEffect(int ball)
{
	const uint8_t action = fEffects.ApplyHitEffect(ball);
	fEffects.PerformHitEffect(action);
}
