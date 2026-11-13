/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "Playfield.h"


Playfield::Playfield(State& state, Screen& screen)
	:
	fState(state),
	fScreen(screen)
{
}


// ERASE_BRICK (0xAB8F). The original computes the brick's first character,
// asks the VDP to write there and sends the two characters of background. The
// name table is at 0x1800, which is also where the port's registers put it.
void
Playfield::EraseBrick()
{
	const uint8_t row = fState.Read(GameRam::kBrickRow);
	const uint8_t column = fState.Read(GameRam::kBrickCol);

	// The original doubles the column in an 8-bit register, so a column
	// above 127 wraps; no brick ever has one, but the port does the same
	// arithmetic rather than a wider one that would silently differ.
	const uint8_t doubledColumn = static_cast<uint8_t>(column << 1);

	// 0x1800 + 2 + 2*32 is where the first brick's first character is, one
	// character row below the top of the frame; each brick row is one
	// character row further down, each brick two characters to the right.
	const uint16_t address = static_cast<uint16_t>(fScreen.NameBase()
		+ 2 + 2 * 32 + 32 * (row + 1) + doubledColumn);

	// TABLE_BACKGROUND_ERASE holds four pointers, chosen by the row; each
	// points at four characters, and the column chooses the first or second
	// pair of them.
	const uint16_t table = GameRam::kTableBackgroundErase;
	const uint16_t entryTable = static_cast<uint16_t>(table
		+ 2 * (row & 3));
	const uint16_t entry = static_cast<uint16_t>(
		fState.Read(entryTable)
		| (fState.Read(entryTable + 1) << 8));

	const uint16_t pair = static_cast<uint16_t>(entry + 2 * (column & 1));
	fScreen.SetVram(address, fState.Read(pair));
	fScreen.SetVram(static_cast<uint16_t>(address + 1),
		fState.Read(static_cast<uint16_t>(pair + 1)));
}
