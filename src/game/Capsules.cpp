/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "Capsules.h"


namespace {

enum {
	kCapsuleNothing			= 5,	// the type that draws nothing
	kCapsuleLife			= 6,
	kCapsulesPerGap			= 33,
	kLivesPerGap			= 0x32,
	kSpawnedRandomNumber	= 0x1f,
	kRandomExhausted		= 0xff,

	kFallingSpritePattern	= 136,
	kTableEntries			= 8,

	kTableAllTypes			= 0xB0BD,	// TBL_FALLING_CAPSULE_TYPES_1
	kTableNoSticky			= 0xB0C5,	// _2
	kTableNoEnlarged		= 0xB0CD,	// _3
	kTableNoLasers			= 0xB0D5,	// _4
	kTableTypeToColour		= 0xB0A5	// TBL_CAPSULE_TYPE_TO_COLOR
};


enum {
	kSpriteY		= 0,
	kSpriteX		= 1,
	kSpritePattern	= 2,
	kSpriteColour	= 3
};

}


Capsules::Capsules(State& state, Screen& screen, Random& random)
	:
	fState(state),
	fScreen(screen),
	fRandom(random)
{
}


// SET_PROPER_RANDOM_CAPSULE_TYPE (0xB028). Below four bricks left no capsule
// appears at all, and only one capsule can be in the air. The two counters
// decide whether this brick gives one; if it does, the type comes from the
// table that matches the vaus and the game's state.
void
Capsules::SpawnFromBrick()
{
	if (fState.Read(GameRam::kBricksLeft) < 4)
		return;

	if (fState.Read(GameRam::kCapsuleIsFalling) != 0)
		return;

	fState.Write(GameRam::kCapsuleIsFalling, 1);

	// Either counter can decide the capsule; only when neither does is a
	// type drawn from one of the four tables. All three paths end at the
	// sprite, so the capsule is placed whatever it turned out to be.
	bool decided = SetRandomCapsuleType();
	if (!decided)
		decided = DecideBrickIsLife();

	if (!decided) {
		uint16_t table = kTableAllTypes;
		if (fState.Read(GameRam::kGluingStatus)
				== GameRam::kGluingStateSticky) {
			table = kTableNoSticky;
		} else if (fState.Read(GameRam::kVausIsEnlarged) != 0) {
			table = kTableNoEnlarged;
		} else if (fState.Read(GameRam::kVausHasLasers) != 0) {
			table = kTableNoLasers;
		}

		SetRandomFallingCapsule(table);
	}

	// The capsule appears where the brick was: eight pixels for each brick
	// row, sixteen for each column, one brick in from the frame.
	const uint16_t sprite = GameRam::kFallingCapsuleSprite;
	fState.Write(sprite + kSpriteY, static_cast<uint8_t>(
		static_cast<uint8_t>(fState.Read(GameRam::kBrickRow) << 3) + 24));
	fState.Write(sprite + kSpriteX, static_cast<uint8_t>(
		static_cast<uint8_t>(fState.Read(GameRam::kBrickCol) << 4) + 16));
	fState.Write(sprite + kSpritePattern, kFallingSpritePattern);
	fState.Write(sprite + kSpriteColour, fState.Read(
		static_cast<uint16_t>(kTableTypeToColour
			+ fState.Read(GameRam::kCapsuleType))));
}


// SET_RANDOM_CAPSULE_TYPE (0xB0DD). While the portal is open no capsule
// appears. The counter runs out, a new random gap is drawn and the number it
// gave is spent one hit at a time; when it is spent, this brick's type is "no
// capsule" and the counter is finished with.
bool
Capsules::SetRandomCapsuleType()
{
	if (fState.Read(GameRam::kPortalOpen) != 0)
		return false;

	uint16_t left = GameRam::kCapsulesLeft;
	if (fState.Read(left) == 0) {
		fState.Write(GameRam::kCapsulesRandomNum,
			static_cast<uint8_t>(fRandom.Next() & kSpawnedRandomNumber));
		fState.Write(left, kCapsulesPerGap);
	}

	fState.Write(left, static_cast<uint8_t>(fState.Read(left) - 1));

	const uint16_t gap = GameRam::kCapsulesRandomNum;
	if (fState.Read(gap) == 0) {
		fState.Write(static_cast<uint16_t>(GameRam::kCapsuleType),
			kCapsuleNothing);
		fState.Write(gap, kRandomExhausted);
		return true;
	}

	fState.Write(gap, static_cast<uint8_t>(fState.Read(gap) - 1));
	return false;
}


// DECIDE_BRICK_IS_LIFE (0xB10A): the same idea for the capsule that is worth a
// life, with its own gap and its own refusal once one has been obtained.
bool
Capsules::DecideBrickIsLife()
{
	if (fState.Read(GameRam::kLifeObtainedFlag) != 0)
		return false;

	uint16_t left = GameRam::kCapsuleLivesCounterL;
	if (fState.Read(left) == 0) {
		fState.Write(GameRam::kCapsuleLivesCounterH,
			static_cast<uint8_t>(fRandom.Next() & kSpawnedRandomNumber));
		fState.Write(left, kLivesPerGap);
	}

	fState.Write(left, static_cast<uint8_t>(fState.Read(left) - 1));

	const uint16_t gap = GameRam::kCapsuleLivesCounterH;
	if (fState.Read(gap) == 0) {
		fState.Write(static_cast<uint16_t>(GameRam::kCapsuleType),
			kCapsuleLife);
		fState.Write(gap, kRandomExhausted);
		return true;
	}

	fState.Write(gap, static_cast<uint8_t>(fState.Read(gap) - 1));
	return false;
}


// SET_RANDOM_FALLING_CAPSULE (0xB0AD): eight entries in each table, and the
// game takes one of them. The original picks the entry from the refresh
// register and the register litter; the port uses its own byte, which changes
// only which of the eight is taken, not the odds.
void
Capsules::SetRandomFallingCapsule(uint16_t table)
{
	const uint8_t index = static_cast<uint8_t>(fRandom.Next() % kTableEntries);
	fState.Write(static_cast<uint16_t>(GameRam::kCapsuleType),
		fState.Read(static_cast<uint16_t>(table + index)));
}
