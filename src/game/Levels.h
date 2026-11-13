/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The level's picture.
//
// A level is a 17-byte bitmask of 132 cells (12 rows of 11 bricks) and a list of
// colours, one per brick that is there. The game assembles the playfield's 12
// rows of 22 characters in RAM first - the background pattern, then a brick
// where the mask says - and copies the result into video memory in one go.
//
// This is ADD_BRICKS_TO_TILEMAP (0x5C15) and the two helpers it calls, and the
// reset it does: the level's brick count, and how many hits its hard bricks
// take, which is LEVEL >> 3 + 2 and differs per group of eight levels.
//
// Not here, because it is somewhere else in the original: the copy of the
// bitmask into BRICK_MAP, which the collision code reads.
//
// A level replayed after a lost life (LEVEL_TRANSITION_SAME) draws from
// BRICK_MAP, the bricks still standing, but spends a colour for every brick the
// *level* has, including the ones already gone: the original re-tests the
// level's own bitmask on every cell it decides not to draw. That is what keeps
// the colours of the remaining bricks where they were, and the port does it too.


#ifndef ARKANOID_LEVELS_H
#define ARKANOID_LEVELS_H


#include <stdint.h>

#include "Screen.h"
#include "State.h"


class Levels {
public:
						Levels(State& state, Screen& screen);

	// ADD_BRICKS_TO_TILEMAP (0x5C15): the background, the level's brick
	// count and hard-brick strength, then the bricks.
	void				AddBricksToTilemap();

	// ADD_BACKGROUND_TO_TILEMAP (0x5D9A): the 12 rows of 22 cells are a
	// four-by-four pattern repeated.
	void				AddBackgroundToTilemap();

	// DRAW_BACKGROUND_TILEMAP (0x5D77): 22 characters of each row into
	// video memory at 0x1862, one 32-character line apart.
	void				DrawBackgroundTilemap();

	// COLORIZE_BACKGROUND (0x5BBD): the three blocks of colour table, each
	// a single byte over 128 entries, picked by the level modulo four.
	void				ColorizeBackground();

	// WRITE_ROUND_MSG (0x7204): "ROUND" and the number, as a decimal
	// counter that starts at 01.
	void				WriteRoundMessage();

	// The copy the round start makes before drawing (0x4E51): the level's
	// 17-byte bitmask into BRICK_MAP, which is what the collision code
	// reads. A replay keeps the map it has.
	void				CopyBrickMap();

private:
	uint16_t			_Read16(uint16_t address) const;

			State&		fState;
			Screen&		fScreen;
};


#endif	// ARKANOID_LEVELS_H
