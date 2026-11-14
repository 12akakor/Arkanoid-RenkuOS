/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "BrickEffects.h"

#include "SoundCodes.h"


namespace {

enum {
	kNextLevel				= 0x02
};

const uint8_t kMaxLivesDrawn = 6;


// Adding one packed decimal byte to another, with the Z80's DAA after an ADD.
uint8_t
AddPackedDecimal(uint8_t value, uint8_t operand, bool carryIn, bool& carryOut)
{
	const int low = (value & 0x0f) + (operand & 0x0f) + (carryIn ? 1 : 0);
	const int sum = value + operand + (carryIn ? 1 : 0);

	const bool halfCarry = low > 0x0f;
	carryOut = sum > 0xff;

	uint8_t result = static_cast<uint8_t>(sum);
	if (halfCarry || (result & 0x0f) > 9)
		result = static_cast<uint8_t>(result + 0x06);
	if (carryOut || (result & 0xf0) > 0x90) {
		result = static_cast<uint8_t>(result + 0x60);
		carryOut = true;
	}
	return result;
}


// TBL_POINTS_PER_BRICK's colour codes, from the tile the brick was drawn with:
// GIVE_BRICK_HIT_POINTS compares the tile against each of these in turn.
struct TileCode {
	uint8_t	tile;
	uint8_t	points;
};

const TileCode kTileCodes[] = {
	{ 0x23, 0 }, { 0x25, 6 }, { 0x27, 2 }, { 0x29, 3 },
	{ 0x5c, 4 }, { 0x5e, 5 }, { 0x61, 1 }, { 0x63, 7 }
};

}


BrickEffects::BrickEffects(State& state, Screen& screen)
	:
	fState(state),
	fScreen(screen),
	fBall(state),
	fBrick(state),
	fPlayfield(state, screen),
	fSound(state),
	fCapsules(state, screen, fRandom)
{
}


uint16_t
BrickEffects::_Read16(uint16_t address) const
{
	return static_cast<uint16_t>(fState.Read(address)
		| (fState.Read(static_cast<uint16_t>(address + 1)) << 8));
}


// APPLY_BRICK_HIT_EFFECT (0xAA05). The level's table offset comes from a table
// of offsets in the cartridge; the brick's byte is at that offset plus
// 11*row + column, which is the brick's place in the level's own table.
uint8_t
BrickEffects::ApplyHitEffect(int ball)
{
	fBall.UpdateSpeed(ball);

	const uint16_t level = fState.Read(GameRam::kLevel);
	const uint16_t offset = _Read16(static_cast<uint16_t>(
		GameRam::kTblBrickActionOffsets + 2 * level));

	// The offset is kept in memory: the actions and the hard-brick table
	// read it back from there.
	fState.Write(GameRam::kBrickActionTableOffset,
		static_cast<uint8_t>(offset & 0xff));
	fState.Write(static_cast<uint16_t>(GameRam::kBrickActionTableOffset + 1),
		static_cast<uint8_t>(offset >> 8));

	uint8_t row = fState.Read(GameRam::kBrickRow);
	if (row >= 12)
		row = 11;
	uint8_t column = fState.Read(GameRam::kBrickCol);
	if (column >= 11)
		column = 10;

	return fState.Read(static_cast<uint16_t>(offset + 11 * row + column));
}


bool
BrickEffects::PerformHitEffect(uint8_t action)
{
	switch (action) {
		case kActionBrickHit:
			ActionBrickHit();
			return true;
		case kActionBrickHitAndCapsule:
			fCapsules.SpawnFromBrick();
			ActionBrickHit();
			return true;
		case kActionHardBrickHit:
			ActionHardBrickHit();
			return true;
		case kActionUnbreakableBrickHit:
			ActionUnbreakableBrickHit();
			return true;
		case kActionResetUnusedVars:
			ResetUnusedVars();
			return true;
		default:
			// An action number outside the original's table.
			return false;
	}
}


// action_reset_unused_vars (0xAA9C).
void
BrickEffects::ResetUnusedVars()
{
	fState.Write(GameRam::kBrickUnused1, 0);
	fState.Write(GameRam::kBrickUnused2, 0);
}


// action_brick_hit (0xAAEF): take the brick out of the mask, count the level
// down and play the sound.
void
BrickEffects::ActionBrickHit()
{
	fState.Write(GameRam::kBrickUnused1, 0);
	fState.Write(GameRam::kBrickUnused2, 0);

	fBrick.Remove(fState.Read(GameRam::kBrickRow),
		fState.Read(GameRam::kBrickCol));
	_DecBricksCheckLevelDone();
	fSound.Add(SoundCodes::kBrickDestroyed);
}


// DEC_BRICKS_CHECK_LEVEL_DONE (0xAB7A): the points first, then a brick fewer.
// On the last brick the level transition is armed and the brick is erased all
// the same, because the original falls into ERASE_BRICK.
void
BrickEffects::_DecBricksCheckLevelDone()
{
	GiveBrickHitPoints();

	const uint8_t left = static_cast<uint8_t>(
		fState.Read(GameRam::kBricksLeft) - 1);
	fState.Write(GameRam::kBricksLeft, left);

	if (left == 0) {
		fState.Write(GameRam::kLevelTransitionType, 0);
		fState.Write(GameRam::kTransitionAction, kNextLevel);
	}

	fPlayfield.EraseBrick();
}


// GIVE_BRICK_HIT_POINTS (0x9604): the brick's colour comes from the tile it was
// drawn with, kept in BACKGROUND_TILEMAP. An unknown tile scores nothing.
void
BrickEffects::GiveBrickHitPoints()
{
	const uint8_t row = fState.Read(GameRam::kBrickRow);
	const uint8_t column = fState.Read(GameRam::kBrickCol);

	const uint16_t offset = static_cast<uint16_t>(22 * row
		+ static_cast<uint8_t>(column << 1));
	const uint8_t tile = fState.Read(static_cast<uint16_t>(
		GameRam::kBackgroundTilemap + offset));

	for (unsigned i = 0; i < sizeof(kTileCodes) / sizeof(kTileCodes[0]); i++) {
		if (tile == kTileCodes[i].tile) {
			AddPoints(kTileCodes[i].points);
			return;
		}
	}

	if (tile == 0x65) {
		// Doh's brick: worth as much as TBL_POINTS_PER_BRICK says for
		// this level.
		const uint8_t index = fState.Read(GameRam::kLevel) & 0xf8;
		AddPoints(fState.Read(static_cast<uint16_t>(
			GameRam::kTblPointsPerBrick + (index >> 3))));
	}
}


// ADD_POINTS_AND_UPDATE_SCORES (0x52A0). The points are a three-byte packed
// decimal entry of POINTS_TABLE, indexed by the colour code. Nothing is added
// in game state 0; in state 3 the points go to the zeros buffer instead of the
// score. Then the high score is raised if it has been passed, and the life
// target is checked either way.
void
BrickEffects::AddPoints(uint8_t points)
{
	const uint16_t entry = static_cast<uint16_t>(GameRam::kPointsTable
		+ 3 * points);
	const uint8_t gameState = fState.Read(GameRam::kGameState);

	if (gameState == 0)
		return;

	if (gameState == 3)
		_AddBcd(GameRam::kZerosBcdBuffer, entry);
	else
		_AddBcd(GameRam::kScoreBcd, entry);

	int comparison = 0;
	for (int i = 2; i >= 0; i--) {
		const uint8_t score = fState.Read(
			static_cast<uint16_t>(GameRam::kScoreBcdBuffer + i));
		const uint8_t high = fState.Read(
			static_cast<uint16_t>(GameRam::kHighScoreBcd + i));
		if (score == high)
			continue;
		comparison = score > high ? 1 : -1;
		break;
	}

	if (comparison > 0) {
		for (int i = 0; i < 3; i++) {
			fState.Write(static_cast<uint16_t>(GameRam::kHighScoreBcd + i),
				fState.Read(static_cast<uint16_t>(
					GameRam::kScoreBcdBuffer + i)));
		}
	}

	_CheckScoreLifeTarget();
}


// BCD_UPDATE_SCORE_ADD_POINTS (0x538A): the score is copied to the buffer, the
// points are added to it a byte at a time with decimal adjust, and the buffer
// is copied back over the score.
void
BrickEffects::_AddBcd(uint16_t score, uint16_t points)
{
	for (int i = 0; i < 3; i++) {
		fState.Write(static_cast<uint16_t>(GameRam::kScoreBcdBuffer + i),
			fState.Read(static_cast<uint16_t>(score + i)));
	}

	bool carry = false;
	for (int i = 0; i < 3; i++) {
		bool carryOut = false;
		const uint8_t result = AddPackedDecimal(
			fState.Read(static_cast<uint16_t>(GameRam::kScoreBcdBuffer + i)),
			fState.Read(static_cast<uint16_t>(points + i)), carry, carryOut);
		fState.Write(static_cast<uint16_t>(GameRam::kScoreBcdBuffer + i),
			result);
		carry = carryOut;
	}

	for (int i = 0; i < 3; i++) {
		fState.Write(static_cast<uint16_t>(score + i),
			fState.Read(static_cast<uint16_t>(GameRam::kScoreBcdBuffer + i)));
	}
}


// CHECK_SCORE_LIFE_TARGET (0x5319): every time the score reaches the target,
// the player gets a life and the target moves on by 40 or 60, in packed
// decimal, depending on whether it is the first extra life of this round.
void
BrickEffects::_CheckScoreLifeTarget()
{
	int comparison = 0;
	for (int i = 2; i >= 0; i--) {
		const uint8_t score = fState.Read(
			static_cast<uint16_t>(GameRam::kScoreBcd + i));
		const uint8_t target = fState.Read(
			static_cast<uint16_t>(GameRam::kScoreLifeBcd + i));
		if (score == target)
			continue;
		comparison = score > target ? 1 : -1;
		break;
	}

	if (comparison < 0)
		return;

	fState.Write(GameRam::kLives,
		static_cast<uint8_t>(fState.Read(GameRam::kLives) + 1));
	DrawLives();
	fSound.Add(SoundCodes::kSoundLife);

	uint16_t counterAddress = static_cast<uint16_t>(GameRam::kScoreLifeBcd + 3);
	const uint8_t counter = static_cast<uint8_t>(
		fState.Read(counterAddress) + 1);
	fState.Write(counterAddress, counter);

	bool carry = false;
	const uint16_t stepAddress = static_cast<uint16_t>(
		GameRam::kScoreLifeBcd + 1);
	fState.Write(stepAddress, AddPackedDecimal(fState.Read(stepAddress),
		counter == 1 ? 0x40 : 0x60, false, carry));

	if (carry) {
		const uint16_t highAddress = static_cast<uint16_t>(
			GameRam::kScoreLifeBcd + 2);
		bool ignored = false;
		fState.Write(highAddress, AddPackedDecimal(
			fState.Read(highAddress), 1, false, ignored));
	}
}


// DRAW_LIVES (0x71B9): up to six lives, one character of the name table each,
// at the addresses the cartridge lists. The two characters themselves are in
// the cartridge too, so the picture is the original's.
void
BrickEffects::DrawLives()
{
	const uint8_t lives = fState.Read(GameRam::kLives);
	if (lives == 0)
		return;

	fState.Write(GameRam::kBrickHitYPixel,
		lives < kMaxLivesDrawn ? lives : kMaxLivesDrawn);
	fState.Write(GameRam::kBrickHitXPixel, 0);

	do {
		const uint8_t index = fState.Read(GameRam::kBrickHitXPixel);
		const uint16_t address = _Read16(static_cast<uint16_t>(
			GameRam::kLivesVdpAddresses + 2 * index));

		uint8_t pattern[2];
		pattern[0] = fState.Read(GameRam::kLivePatterns);
		pattern[1] = fState.Read(static_cast<uint16_t>(
			GameRam::kLivePatterns + 1));
		fScreen.CopyToVram(address, pattern, 2);

		fState.Write(GameRam::kBrickHitXPixel,
			static_cast<uint8_t>(index + 1));
		const uint8_t remaining = static_cast<uint8_t>(
			fState.Read(GameRam::kBrickHitYPixel) - 1);
		fState.Write(GameRam::kBrickHitYPixel, remaining);
		if (remaining == 0)
			break;
	} while (true);
}


// action_hard_brick_hit (0xAAC7): the brick's own counter goes down; while it
// is not zero the brick only bounces the ball, otherwise it is destroyed like
// any other.
void
BrickEffects::ActionHardBrickHit()
{
	fState.Write(GameRam::kBrickUnused1, 0);
	fState.Write(GameRam::kBrickUnused2, 1);

	const uint8_t row = fState.Read(GameRam::kBrickRow);
	const uint8_t column = fState.Read(GameRam::kBrickCol);

	const uint16_t counter = static_cast<uint16_t>(
		GameRam::kHardBricksRemainingHits + 11 * row + column);
	const uint8_t remaining = static_cast<uint8_t>(
		fState.Read(counter) - 1);
	fState.Write(counter, remaining);

	if (remaining != 0)
		_HardBrickBounce(0);
	else
		ActionBrickHit();
}


// action_unbreakable_brick_hit (0xAAA4): an unbreakable brick is never
// destroyed, but every twentieth blow turns all the balls' skewness round.
void
BrickEffects::ActionUnbreakableBrickHit()
{
	fState.Write(GameRam::kBrickUnused2, 0);
	fState.Write(GameRam::kBrickUnused1, 1);

	const uint8_t count = static_cast<uint8_t>(
		fState.Read(GameRam::kActionSkewnessCounter) + 1);
	fState.Write(GameRam::kActionSkewnessCounter, count);

	if (count == 20) {
		fState.Write(GameRam::kActionSkewnessCounter, 0);
		fBall.ChangeBallsSkewness();
	}

	_HardBrickBounce(1);
}


// action_hard_brick_bounce (0xAB06): the brick stays, so it starts its own
// animation. The address of its first character, which the table keeps, is
// computed here; BRICK_HIT_X/Y_PIXEL carry the row and column along.
void
BrickEffects::_HardBrickBounce(uint8_t kind)
{
	const uint8_t row = fState.Read(GameRam::kBrickRow);
	const uint8_t column = fState.Read(GameRam::kBrickCol);

	const uint16_t vram = static_cast<uint16_t>(fScreen.NameBase()
		+ 2 + 2 * 32 + 32 * (row + 1)
		+ static_cast<uint8_t>(column << 1));

	fState.Write(GameRam::kBrickHitYPixel, row);
	fState.Write(GameRam::kBrickHitXPixel, column);

	_UpdateHardBrickTable(kind, vram);
	fSound.Add(SoundCodes::kHardBrickHit);
}


// UPDATE_HARD_BRICK_TABLE (0x97AF): the first free entry of the eight is taken
// for this brick, with its kind, its address, the row and column, and the
// animation's counters at zero.
void
BrickEffects::_UpdateHardBrickTable(uint8_t kind, uint16_t vram)
{
	for (int i = 0; i < 8; i++) {
		const uint16_t entry = static_cast<uint16_t>(
			GameRam::kHardBrickTable + 8 * i);
		if (fState.Read(entry) != 0)
			continue;

		fState.Write(entry, 1);
		fState.Write(static_cast<uint16_t>(entry + 1), kind);
		fState.Write(static_cast<uint16_t>(entry + 2),
			static_cast<uint8_t>(vram & 0xff));
		fState.Write(static_cast<uint16_t>(entry + 3),
			static_cast<uint8_t>(vram >> 8));
		fState.Write(static_cast<uint16_t>(entry + 4), 0);
		fState.Write(static_cast<uint16_t>(entry + 5), 0);
		fState.Write(static_cast<uint16_t>(entry + 6),
			fState.Read(GameRam::kBrickHitYPixel));
		fState.Write(static_cast<uint16_t>(entry + 7),
			fState.Read(GameRam::kBrickHitXPixel));
		return;
	}
}
