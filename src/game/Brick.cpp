/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#include "Brick.h"


namespace {

// Cartridge tables (see BRICK_EXISTS_AT_ROWCOL).
enum {
	kBrickLevelBitmask	= 0xAC10,	// 16-bit pointer per (row, column)
	kBrickJumpTable		= 0xAE01	// 16-bit handler address per cell
};


// A cell's row index into the tables is 32 bytes, its column index two more.
enum {
	kRowStride		= 32,
	kColumnStride	= 2
};

}	// namespace


Brick::Brick(State& state)
	:
	fState(state)
{
}


// BRICK_EXISTS_AT_ROWCOL (0xADA8), the test half. The original's other half
// clears the bit, but it is reached from ERASE_BRICK's own copy of the code,
// not from here.
bool
Brick::ExistsAt(uint8_t row, uint8_t column)
{
	// Rows past the field are clamped to the last one; the column is
	// clamped in the second table's lookup only, exactly as the original
	// does it.
	const uint8_t cappedRow = row < kRows ? row : kRows - 1;

	// IY = BRICK_LEVEL_BITMASK[32*row + 2*column]: the byte of the bitmask
	// this cell lives in. The cartridge's entry for column 11 is 0x0000, so
	// asking for it makes the original read the BIOS page; the collision
	// code only asks for columns 0 to 10.
	const uint16_t byteAddress = _Read16(kBrickLevelBitmask
		+ kRowStride * cappedRow + kColumnStride * column);

	const uint8_t cappedColumn = column < kColumns ? column : kColumns - 1;
	const uint16_t handler = _Read16(kBrickJumpTable
		+ kRowStride * cappedRow + kColumnStride * cappedColumn);
	const int bit = _BitOfHandler(handler);

	const bool present = (fState.Read(byteAddress) & (1 << bit)) != 0;

	// DOH_BEEN_HIT tells Doh's level that the ball touched a brick.
	fState.Write(GameRam::kDohBeenHit, present ? 1 : 0);

	return present;
}


// CHECK_AND_REMOVE_BRICK (0xABD3). The original builds IY from the bitmask
// table and jumps through the same jump table as the test, but with A = 0, so
// the handler takes its `res` branch and returns. Nothing is clamped and
// DOH_BEEN_HIT is left alone.
void
Brick::Remove(uint8_t row, uint8_t column)
{
	const uint16_t byteAddress = _Read16(kBrickLevelBitmask
		+ kRowStride * row + kColumnStride * column);
	const uint16_t handler = _Read16(kBrickJumpTable
		+ kRowStride * row + kColumnStride * column);
	const int bit = _BitOfHandler(handler);

	fState.Write(byteAddress,
		static_cast<uint8_t>(fState.Read(byteAddress) & ~(1 << bit)));
}


// The eight handlers are the same code with a different bit: read the bit out
// of the handler's own instruction instead of keeping a copy of the mapping.
// `res n,(iy+0)` is FD CB 00 (0x86 | 8n) and `bit n,(iy+0)` is FD CB 00
// (0x46 | 8n), so bits 3 to 5 of either opcode are the bit number.
int
Brick::_BitOfHandler(uint16_t handler) const
{
	for (uint16_t address = handler; address < handler + 0x20; address++) {
		const uint8_t opcode = fState.Read(address);
		if ((opcode & 0xc7) == 0x86 || (opcode & 0xc7) == 0x46)
			return (opcode >> 3) & 0x07;
	}
	return 7;
}


uint16_t
Brick::_Read16(uint16_t address) const
{
	return static_cast<uint16_t>(fState.Read(address)
		| (fState.Read(address + 1) << 8));
}
