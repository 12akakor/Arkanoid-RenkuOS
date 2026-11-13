/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "Levels.h"


Levels::Levels(State& state, Screen& screen)
	:
	fState(state),
	fScreen(screen)
{
}


uint16_t
Levels::_Read16(uint16_t address) const
{
	return static_cast<uint16_t>(fState.Read(address)
		| (fState.Read(address + 1) << 8));
}


// ADD_BRICKS_TO_TILEMAP (0x5C15). A new level brings its brick count and its
// hard-brick strength; a level replayed after a lost life keeps them, because
// the bricks that are left are the ones that were there.
void
Levels::AddBricksToTilemap()
{
	AddBackgroundToTilemap();

	const uint8_t level = fState.Read(GameRam::kLevel);
	const bool sameLevel = fState.Read(GameRam::kLevelTransitionType)
		== GameRam::kLevelTransitionSame;

	if (!sameLevel) {
		fState.Write(GameRam::kBricksLeft,
			fState.Read(GameRam::kBricksPerLevel + level));

		// How many hits a hard brick takes: one more for every eight
		// levels, starting at two.
		const uint8_t hits = static_cast<uint8_t>((level >> 3) + 2);
		for (int cell = 0; cell < 12 * 11; cell++)
			fState.Write(GameRam::kHardBricksRemainingHits + cell, hits);
	}

	// The cells to draw: the level's own mask, or the map of the bricks
	// still standing when the same level is played again. The colour list is
	// always the level's, and a colour is taken for every brick the *level*
	// has, even where the replay finds it already gone - which is what the
	// original does, and why the colours keep their places.
	const uint16_t levelBits = _Read16(GameRam::kLevelsPtrTable + 2 * level);
	const uint16_t drawnBits = sameLevel ? 0xE027 : levelBits;	// BRICK_MAP

	uint16_t colours = _Read16(GameRam::kLevelColorsPtrTable + 2 * level);
	uint16_t tilemap = GameRam::kBackgroundTilemap;

	// Seventeen bytes of eight cells: 136 for 132 cells, and the four spare
	// bits at the end are clear in every level. The colour list has one byte
	// per brick that is there, in the same order.
	for (int byte = 0; byte < 17; byte++) {
		const uint8_t drawn = fState.Read(drawnBits + byte);
		const uint8_t present = fState.Read(levelBits + byte);
		for (int bit = 7; bit >= 0; bit--) {
			if ((drawn & (1 << bit)) != 0) {
				const uint16_t pattern = static_cast<uint16_t>(
					GameRam::kTblColorToPattern
						+ 2 * fState.Read(colours));
				fState.Write(tilemap, fState.Read(pattern));
				fState.Write(tilemap + 1, fState.Read(pattern + 1));
				colours++;
			} else if ((present & (1 << bit)) != 0) {
				// The level has a brick here: its colour is spent
				// even though the level being replayed no longer does.
				colours++;
			}
			tilemap = static_cast<uint16_t>(tilemap + 2);
		}
	}

	// The three counters the walk keeps, left as it leaves them: the offset
	// back to zero, the bit inside the byte back to zero (136 bits), and the
	// byte counter at the seventeen it walked.
	fState.Write(0xE486, 0);		// BRICK_TILEMAP_OFFSET
	fState.Write(0xE487, 0);
	fState.Write(0xE489, 0);		// BRICK_BIT_COUNT
	fState.Write(0xE48A, 17);		// BRICK_BLOCK
}


// ADD_BACKGROUND_TO_TILEMAP (0x5D9A): a four-by-four block of characters,
// tiled over the whole 12 rows of 22 cells.
void
Levels::AddBackgroundToTilemap()
{
	uint16_t tilemap = GameRam::kBackgroundTilemap;
	for (int row = 0; row < GameRam::kTilemapRows; row++) {
		for (int column = 0; column < GameRam::kTilemapWidth; column++) {
			const uint16_t source = static_cast<uint16_t>(
				GameRam::kTableBackgroundEntry1 + 4 * (row & 3)
					+ (column & 3));
			fState.Write(tilemap++, fState.Read(source));
		}
	}

	// The two counters are left where the loops end: twelve rows, twenty-two
	// columns.
	fState.Write(GameRam::kBrickHitXPixel, GameRam::kTilemapWidth);
	fState.Write(GameRam::kBrickHitYPixel, GameRam::kTilemapRows);
}


// DRAW_BACKGROUND_TILEMAP (0x5D77): the 12 rows into video memory, each one
// starting 32 characters after the one above.
void
Levels::DrawBackgroundTilemap()
{
	for (int row = 0; row < GameRam::kTilemapRows; row++) {
		const uint16_t source = static_cast<uint16_t>(GameRam::kBackgroundTilemap
			+ row * GameRam::kTilemapWidth);
		const uint16_t target = static_cast<uint16_t>(GameRam::kTilemapVramAddress
			+ row * GameRam::kTilemapStride);
		for (int column = 0; column < GameRam::kTilemapWidth; column++)
			fScreen.SetVram(target + column, fState.Read(source + column));
	}
}


// COLORIZE_BACKGROUND (0x5BBD). The playfield has no per-cell colours of its
// own: each of the three 2 KB blocks of the colour table is filled with one
// byte, chosen from four by the level. The last level keeps the byte the third
// block had, which is how Doh's screen gets its single colour.
void
Levels::ColorizeBackground()
{
	const uint8_t level = fState.Read(GameRam::kLevel);
	uint8_t byte = static_cast<uint8_t>(16 * 8 + 1);

	for (int block = 0; block < 3; block++) {
		if (level != GameRam::kFinalLevel) {
			byte = fState.Read(GameRam::kBackgroundColours
				+ (level & 3));
		}
		fScreen.FillVram(static_cast<uint16_t>(GameRam::kColourTableVram
			+ block * 2048), byte, 16 * 8);
	}
}


// WRITE_ROUND_MSG (0x7204). The number is a decimal counter kept in binary
// coded decimal and shown with one leading zero: level 0 is "ROUND 01".
void
Levels::WriteRoundMessage()
{
	const uint16_t name = fScreen.NameBase();

	for (int i = 0; i < 5; i++) {
		fScreen.SetVram(static_cast<uint16_t>(name + 26 + 22 * 32 + i),
			fState.Read(GameRam::kRoundString + i));
	}

	// The counter, incremented and then corrected for decimal: the Z80's
	// DAA after an addition.
	uint8_t number = static_cast<uint8_t>(
		fState.Read(GameRam::kLevelDispBcd) + 1);
	bool carry = false;
	if ((number & 0x0f) > 9) {
		const uint8_t before = number;
		number = static_cast<uint8_t>(number + 6);
		carry = number < before;
	}
	if (carry || (number >> 4) > 9)
		number = static_cast<uint8_t>(number + 0x60);

	const uint8_t tens = (number >> 4) + 0x30;
	fScreen.SetVram(static_cast<uint16_t>(name + 29 + 23 * 32),
		tens == 0x30 ? ' ' : tens);
	fScreen.SetVram(static_cast<uint16_t>(name + 30 + 23 * 32),
		static_cast<uint8_t>((number & 0x0f) + 0x30));
}


// The level's bitmask copied into the map the game plays on (0x4E51). A replay
// after a lost life keeps the map it already has.
void
Levels::CopyBrickMap()
{
	if (fState.Read(GameRam::kLevelTransitionType)
			== GameRam::kLevelTransitionSame)
		return;

	const uint8_t level = fState.Read(GameRam::kLevel);
	const uint16_t bits = _Read16(GameRam::kLevelsPtrTable + 2 * level);
	for (int i = 0; i < 17; i++)
		fState.Write(0xE027 + i, fState.Read(bits + i));
}
