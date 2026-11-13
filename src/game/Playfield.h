/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The playfield: the bricks as they appear in the name table.
//
// A brick is two characters wide and sits in two character rows, so erasing
// one means putting back the background the brick was drawn over. The original
// keeps four two-character pairs for that, one per brick row modulo four, and
// picks between the two of each pair with the brick's column. The pairs are
// read from the cartridge, so the picture is the original's own.


#ifndef ARKANOID_PLAYFIELD_H
#define ARKANOID_PLAYFIELD_H


#include <stdint.h>

#include "Screen.h"
#include "State.h"


class Playfield {
public:
						Playfield(State& state, Screen& screen);

	// ERASE_BRICK (0xAB8F): put the background back where the brick with
	// BRICK_ROW and BRICK_COL was.
	void				EraseBrick();

private:
	State&				fState;
	Screen&				fScreen;
};


#endif	// ARKANOID_PLAYFIELD_H
