/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef ARKANOID_BRICK_H
#define ARKANOID_BRICK_H


#include <stdint.h>

#include "State.h"


class State;


// The bricks' presence, ported from BRICK_EXISTS_AT_ROWCOL (0xADA8).
//
// The game does not keep a byte per brick. It packs the whole 12x11 brick
// grid into 17 bytes at 0xE027, with the bits running diagonally across the
// bytes, and finds a brick through two tables in the cartridge: one gives the
// byte the brick lives in, the other, by way of the bit handler it names, the
// bit inside that byte. Both tables are read from the ROM here, at their own
// addresses, so the port uses the cartridge's data rather than a copy.
class Brick {
public:
	static const uint8_t kRows = 12;
	static const uint8_t kColumns = 11;

						Brick(State& state);

	// True when there is a brick at that row and column. Sets DOH_BEEN_HIT
	// as the original does: it is the flag Doh's level reads.
	bool				ExistsAt(uint8_t row, uint8_t column);

	// CHECK_AND_REMOVE_BRICK (0xABD3): the same lookup, with the handler
	// in its clearing mode - `res n,(iy+0)` and nothing else. Unlike the
	// test half it clamps nothing, because the original does not either.
	void				Remove(uint8_t row, uint8_t column);

private:
	// The bit a cell uses is decided by the handler the jump table names;
	// the handler's own RES/BIT instruction says which bit that is.
	int					_BitOfHandler(uint16_t handler) const;
	uint16_t			_Read16(uint16_t address) const;

			State&		fState;
};


#endif	// ARKANOID_BRICK_H
