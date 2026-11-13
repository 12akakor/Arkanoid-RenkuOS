/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "Aliens.h"


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

}


Aliens::Aliens(State& state, Screen& screen)
	:
	fState(state),
	fScreen(screen),
	fBall(state),
	fSound(state),
	fEffects(state, screen)
{
}


// CHECK_ANY_BALL_HITS_ALIEN (0x79A5). Each ball in play is tested; a ball that
// hit an alien has its vertical skewness inverted, so the collision is also a
// change of direction.
void
Aliens::CheckAnyBallHitsAlien()
{
	for (int ball = 0; ball < GameRam::kBallCount; ball++) {
		if (fState.Read(BallTable(ball) + GameRam::kActive) == 0)
			continue;
		if (CheckBallHitsAlien(ball))
			continue;

		fBall.InvertVerticalSkewness(ball);
		fSound.Add(GameRam::kSoundAlienDestroyed);
	}
}


// CHECK_BALL_HITS_ALIEN (0x79FD). The ball's box is sixteen pixels to the left
// and up from its sprite position and four to the right and down, so it is
// wider than the sprite that draws it. The alien is a point.
bool
Aliens::CheckBallHitsAlien(int ball)
{
	const uint16_t sprite = BallSprite(ball);
	const uint8_t ballY = fState.Read(sprite + GameRam::kSpriteY);
	const uint8_t ballX = fState.Read(sprite + GameRam::kSpriteX);

	uint16_t alienSprite = GameRam::kAlienSpriteParams;
	uint16_t entry = GameRam::kAlienTable + GameRam::kAlienActive;

	for (int alien = 0; alien < 3; alien++) {
		if (fState.Read(entry) == 1
			&& fState.Read(alienSprite + GameRam::kSpriteColour) != 0) {
			const uint8_t alienY
				= fState.Read(alienSprite + GameRam::kSpriteY);
			const uint8_t alienX
				= fState.Read(alienSprite + GameRam::kSpriteX);

			if (static_cast<uint8_t>(ballY - 16) < alienY
				&& static_cast<uint8_t>(ballY + 4) >= alienY
				&& static_cast<uint8_t>(ballX - 16) < alienX
				&& static_cast<uint8_t>(ballX + 4) >= alienX) {
				fEffects.AddPoints(5);
				fState.Write(entry, GameRam::kAlienActiveExploding);
				fState.Write(entry + 1, GameRam::kAlienExplodingFlag);
				return false;
			}
		}

		alienSprite += GameRam::kSpriteParamsLength;
		entry += GameRam::kAlienTableLength;
	}

	return true;
}


// CHECK_DOH_HIT_AND_BOUNCE_BALL (0x967B). The face is a box from (76, 19) to
// (128, 119). The side the ball came from is read from the position it was at
// before the step, and the ball is put back just outside the box so it does not
// enter it. Every bounce counts, and the sixteenth one finishes Doh off.
void
Aliens::CheckDohHitAndBounceBall(int ball)
{
	const uint16_t table = BallTable(ball);
	const uint16_t sprite = BallSprite(ball);

	if (fState.Read(table + GameRam::kActive) == 0)
		return;

	const uint8_t y = fState.Read(sprite + GameRam::kSpriteY);
	if (y < 19 || y >= 120)
		return;

	const uint8_t x = fState.Read(sprite + GameRam::kSpriteX);
	if (x < 76 || x >= 129)
		return;

	const uint8_t ySpeed = fState.Read(table + GameRam::kYSpeed);
	const uint8_t xSpeed = fState.Read(table + GameRam::kXSpeed);

	const uint8_t prevX = static_cast<uint8_t>(x - xSpeed);
	if (prevX < 77) {
		if ((xSpeed & 0x80) != 0)
			return;
		fBall.HorizontalBounce(ball);
		fState.Write(sprite + GameRam::kSpriteX, 0x4b);
	} else if (prevX >= 128) {
		if ((xSpeed & 0x80) == 0)
			return;
		fBall.HorizontalBounce(ball);
		fState.Write(sprite + GameRam::kSpriteX, 129);
	} else {
		const uint8_t prevY = static_cast<uint8_t>(y - ySpeed);
		if (prevY < 19) {
			// It came from above the face, so it is going down.
			if ((ySpeed & 0x80) != 0)
				return;
			fBall.VerticalBounce(ball);
			fState.Write(sprite + GameRam::kSpriteY, 18);
		} else {
			if ((ySpeed & 0x80) == 0)
				return;
			fBall.VerticalBounce(ball);
			fState.Write(sprite + GameRam::kSpriteY, 119);
		}
	}

	fSound.Add(GameRam::kSoundDohHit);
	fState.Write(GameRam::kDohBeenHit, 1);

	const uint8_t hits
		= static_cast<uint8_t>(fState.Read(GameRam::kDohReceivedHits) + 1);
	fState.Write(GameRam::kDohReceivedHits, hits);
	if (hits != 16) {
		fState.Write(GameRam::kTblDohHit, 1);
		return;
	}

	fState.Write(GameRam::kDohTable, 1);
	DeactiveAllBalls();
	fSound.Add(GameRam::kSoundDohDefeated);
}


// DEACTIVE_ALL_BALLS (0x9710): all three tables' active flags, and all three
// sprites moved to the unused row 192.
void
Aliens::DeactiveAllBalls()
{
	for (int ball = 0; ball < GameRam::kBallCount; ball++)
		fState.Write(BallTable(ball) + GameRam::kActive, 0);
	for (int ball = 0; ball < GameRam::kBallCount; ball++)
		fState.Write(BallSprite(ball) + GameRam::kSpriteY, 192);
}
