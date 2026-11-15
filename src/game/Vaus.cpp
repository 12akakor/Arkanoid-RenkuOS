/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "Vaus.h"


Vaus::Vaus(State& state, Screen& screen)
	:
	fState(state),
	fScreen(screen),
	fSound(state),
	fEffects(state, screen)
{
}


// EXECUTE_VAUS_ACTION (0x68C4), for the states that are ported: waiting and
// following the controls. False means the state was one of the others and
// nothing was done, which the caller has to take as "not yet".
bool
Vaus::ExecuteAction()
{
	// The four sprites that make Vaus are put back where the start-up data
	// has them, once.
	if (fState.Read(GameRam::kResetVausSpritePosition) != 1) {
		for (int i = 0; i < 4 * GameRam::kSpriteParamsLength; i++) {
			fState.Write(GameRam::kSpriteParamsBase + i,
				fState.Read(GameRam::kArkanoidCenteredSprites + i));
		}
		fState.Write(GameRam::kResetVausSpritePosition, 1);
	}

	const uint8_t action = fState.Read(GameRam::kVausTable
		+ GameRam::kVausIdxActionState);
	// The wait state only becomes KEEP; it does not move Vaus this tick.
	if (action == 0) {
		fState.Write(GameRam::kVausTable + GameRam::kVausIdxActionState,
			GameRam::kVausActionStateKeep);
		return true;
	}
	if (action == GameRam::kActionStateExploding) {
		_Destroy();
		return true;
	}
	if (action != GameRam::kVausActionStateKeep)
		return false;

	// Where Vaus wants to be: over the demo's ball, at the paddle, or one step
	// from where it is according to the controls.
	uint8_t x;
	if (fState.Read(GameRam::kGameState) == 0) {
		x = static_cast<uint8_t>(fState.Read(GameRam::kBallXDemo) - 16);
	} else if (fState.Read(GameRam::kUseVausPaddle) != 0) {
		x = _ReadPaddle();
	} else {
		const uint8_t controls = fState.Read(GameRam::kControls) & 0x0f;
		const uint8_t delta = fState.Read(
			GameRam::kIncrementPosVausControls + 2 * controls + 1);
		x = static_cast<uint8_t>(
			fState.Read(GameRam::kSpriteParamsBase + GameRam::kSpriteX)
				+ delta);
	}
	_Store(x);

	if (fState.Read(GameRam::kVausTable + GameRam::kVausIdxResizing) != 0)
		return false;

	// The limits. A Vaus at the right cannot go past 152, and one that would
	// leave the field on either side is put at 8.
	x = fState.Read(GameRam::kVausX2);
	if (x >= 153 && x < 230) {
		x = 152;
		fState.Write(GameRam::kVausX2, x);
		if (fState.Read(GameRam::kPortalOpen) != 0) {
			fState.Write(GameRam::kVausTable + GameRam::kVausIdxActionState,
				7);
			fSound.Add(GameRam::kSoundVausThroughPortal);
			fEffects.AddPoints(12);

			// DEACTIVE_ALL_BALLS: all three tables, all three sprites.
			for (int ball = 0; ball < GameRam::kBallCount; ball++) {
				fState.Write(GameRam::kBallTables
					+ ball * GameRam::kBallTableLength
					+ GameRam::kActive, 0);
				fState.Write(GameRam::kBallSprites
					+ ball * GameRam::kSpriteLength
					+ GameRam::kSpriteY, 192);
			}
			x = fState.Read(GameRam::kVausX2);
		}
	}

	if (x >= 240 || x < 9) {
		x = 8;
		fState.Write(GameRam::kVausX2, x);
	}

	_PutSprites(x);
	return true;
}


// The keyboard's paddle is the paddle the MSX reads: an analogue value scaled
// to 8..176, and 8 when it is below 160 - the leftmost value the original can
// produce.
uint8_t
Vaus::_ReadPaddle()
{
	const uint16_t count = static_cast<uint16_t>(
		fState.Read(GameRam::kPaddleCount)
			| (fState.Read(GameRam::kPaddleCount + 1) << 8));

	uint8_t position = 8;
	if (count >= 160) {
		const uint16_t scaled = static_cast<uint16_t>(count - 156);
		position = scaled >= 176 ? 176 : static_cast<uint8_t>(scaled);
	}

	fState.Write(GameRam::kVausOldX,
		fState.Read(GameRam::kSpriteParamsBase + GameRam::kSpriteX));
	fState.Write(GameRam::kSpriteParamsBase + GameRam::kSpriteX, position);
	fState.Write(GameRam::kVausX2, position);
	return position;
}


void
Vaus::_Store(uint8_t x)
{
	fState.Write(GameRam::kSpriteParamsBase + GameRam::kSpriteX, x);
	fState.Write(GameRam::kVausX2, x);
}


// The Vaus is four 16-pixel-wide sprites. Without lasers they are 16 apart;
// with them the middle two overlap, because the laser turrets make the drawn
// Vaus wider than the plain one.
void
Vaus::_PutSprites(uint8_t x)
{
	const bool hasLaser
		= fState.Read(GameRam::kVausTable + GameRam::kVausIdxHasLaser) == 1;

	const uint8_t offset[4] = { 0, 16, static_cast<uint8_t>(hasLaser ? 16 : 32),
		static_cast<uint8_t>(hasLaser ? 32 : 48) };

	for (int sprite = 0; sprite < 4; sprite++) {
		fState.Write(GameRam::kSpriteParamsBase
			+ sprite * GameRam::kSpriteParamsLength + GameRam::kSpriteX,
			static_cast<uint8_t>(x + offset[sprite]));
	}
}


uint16_t
Vaus::_Sprite(int index)
{
	return static_cast<uint16_t>(GameRam::kSpriteParamsBase
		+ index * GameRam::kSpriteParamsLength);
}


// VAUS_ACTION_STATE_EXPLODING (0x6E0E, vaus_destroyed). Four steps of twelve
// frames each. The first breaks the four sprites that make the Vaus into
// pieces and paints them red and grey; the second sends them drifting apart;
// the third and the fourth swap the pieces' patterns for the debris shapes. The
// end of the fourth puts the Vaus back to waiting, hides the seven sprites it
// used and sends the game on to the same level, where a life is taken.
void
Vaus::_Destroy()
{
	const uint16_t table = GameRam::kVausTable;
	fState.Write(table + GameRam::kVausIdxHasLaser, 0);

	// Twelve frames per step.
	uint8_t step1 = static_cast<uint8_t>(
		fState.Read(table + GameRam::kVausIdxDestructionStep1) + 1);
	fState.Write(table + GameRam::kVausIdxDestructionStep1, step1);
	if (step1 != 12)
		return;

	fState.Write(table + GameRam::kVausIdxDestructionStep1, 0);

	const uint8_t step = static_cast<uint8_t>(
		fState.Read(table + GameRam::kVausIdxDestructionStep2) + 1);
	fState.Write(table + GameRam::kVausIdxDestructionStep2, step);

	if (step == 1) {
		// The pieces are laid out to the right of the Vaus's left sprite,
		// sixteen pixels apart, and given their first colours and patterns.
		const uint8_t x = fState.Read(_Sprite(1) + GameRam::kSpriteX);
		fState.Write(_Sprite(2) + GameRam::kSpriteX,
			static_cast<uint8_t>(x + 16));
		fState.Write(_Sprite(3) + GameRam::kSpriteX,
			static_cast<uint8_t>(x + 32));

		fState.Write(_Sprite(0) + GameRam::kSpriteColour, 8);
		fState.Write(_Sprite(1) + GameRam::kSpriteColour, 14);
		fState.Write(_Sprite(2) + GameRam::kSpriteColour, 8);
		fState.Write(_Sprite(3) + GameRam::kSpriteColour, 0);

		fState.Write(_Sprite(0) + GameRam::kSpritePattern, 32);
		fState.Write(_Sprite(1) + GameRam::kSpritePattern, 36);
		fState.Write(_Sprite(2) + GameRam::kSpritePattern, 40);
		fState.Write(_Sprite(3) + GameRam::kSpritePattern, 1);

		// The original sets these three again, having just done so.
		fState.Write(_Sprite(2) + GameRam::kSpriteColour, 8);
		fState.Write(_Sprite(3) + GameRam::kSpriteColour, 0);
		fState.Write(_Sprite(0) + GameRam::kSpriteColour, 8);
		return;
	}

	// Only the second step moves the pieces: the right-hand three drift left
	// eight pixels a frame, and three more sprites are placed above the Vaus's
	// left sprite, eight, twenty-four and thirty-two pixels to its right.
	if (step < 3) {
		for (int sprite = 1; sprite <= 3; sprite++) {
			const uint16_t params = _Sprite(sprite);
			fState.Write(params + GameRam::kSpriteX,
				static_cast<uint8_t>(fState.Read(params + GameRam::kSpriteX)
					- 8));
		}
		fState.Write(_Sprite(3) + GameRam::kSpriteColour, 8);

		const uint8_t x = fState.Read(_Sprite(0) + GameRam::kSpriteX);
		const uint8_t y = fState.Read(_Sprite(0) + GameRam::kSpriteY);
		for (int sprite = 4; sprite <= 6; sprite++) {
			fState.Write(_Sprite(sprite) + GameRam::kSpriteX, x);
			fState.Write(_Sprite(sprite) + GameRam::kSpriteY, y);
		}

		const uint8_t right[3] = { 8, 24, 32 };
		for (int i = 0; i < 3; i++) {
			const uint16_t params = _Sprite(4 + i);
			fState.Write(params + GameRam::kSpriteX,
				static_cast<uint8_t>(fState.Read(params + GameRam::kSpriteX)
					+ right[i]));
			fState.Write(params + GameRam::kSpriteY,
				static_cast<uint8_t>(fState.Read(params + GameRam::kSpriteY)
					- 16));
		}

		fState.Write(_Sprite(2) + GameRam::kSpriteColour, 14);
		fState.Write(_Sprite(4) + GameRam::kSpriteColour, 14);
		fState.Write(_Sprite(5) + GameRam::kSpriteColour, 14);
		fState.Write(_Sprite(6) + GameRam::kSpriteColour, 8);
	}

	// VAUS_DESTROYED_PATTERNS (0x7019): a list of seven patterns per step. The
	// second and the fourth share one, which is the Vaus's own pieces falling.
	static const uint8_t kPieces[7] = { 0x38, 0x3C, 0x40, 0x44, 0x2C, 0x30,
		0x34 };
	static const uint8_t kDebris[7] = { 0x54, 0x58, 0x5C, 0x60, 0x48, 0x4C,
		0x50 };
	const uint8_t* patterns = step == 3 ? kDebris : kPieces;
	for (int sprite = 0; sprite < 7; sprite++) {
		fState.Write(_Sprite(sprite) + GameRam::kSpritePattern,
			patterns[sprite]);
	}

	if (step != 4)
		return;

	// The end of the fourth step.
	fState.Write(table + GameRam::kVausIdxDestructionStep2, 0);
	fState.Write(table + GameRam::kVausIdxActionState,
		GameRam::kVausActionStateWait);
	for (int sprite = 0; sprite < 7; sprite++)
		fState.Write(_Sprite(sprite) + GameRam::kSpriteColour, 0);

	fState.Write(GameRam::kVausX2, 0);
	fState.Write(table + GameRam::kVausIdxHasLaser, 0);
	fState.Write(GameRam::kLevelTransitionType, GameRam::kLevelTransitionSame);
	fState.Write(GameRam::kTransitionAction, GameRam::kLevelTransitionSame);
}
