/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "TileColors.h"

#include "Screen.h"


// DECOMPRESS_TILE_COLORS (0x4389). One third of the colour table at a time: HL
// is where it goes and the routine stops when HL's high byte reaches H + 8, so
// 0x800 bytes.
//
// A byte with anything in its high nibble is written as it is. A byte below 16
// starts a run: its low nibble is C, the byte after it is B, and the two after
// those are written alternately - value, then value' - once per step of B's
// countdown, so B pairs, two bytes each.
//
// When B runs out C is decremented and the loop goes on while C is positive,
// with B at zero and its countdown running through 256 pairs. That is what the
// cartridge does with its `djnz` / `dec c` / `jp p`, and the streams carry a
// single run per token, so the outer turn is the one that ends it.
int
TileColors::Decompress(const uint8_t* compressed, Screen& screen,
	uint16_t vramAddress)
{
	const uint8_t* data = compressed;
	uint16_t address = vramAddress;
	const uint8_t limit = static_cast<uint8_t>((vramAddress >> 8) + 8);
	int written = 0;

	for (;;) {
		if ((address >> 8) == limit)
			return written;

		if ((*data & 0xF0) != 0) {
			screen.SetVram(address++, *data++);
			written++;
			continue;
		}

		uint8_t countLow = static_cast<uint8_t>(*data++ & 0x0F);
		uint8_t countHigh = *data++;
		const uint8_t value = *data++;
		const uint8_t value2 = *data++;

		// The cartridge tests B with an increment and a decrement, and takes a
		// step off C before the loop when B is zero.
		if (countHigh == 0)
			countLow = static_cast<uint8_t>(countLow - 1);

		for (;;) {
			do {
				screen.SetVram(address++, value);
				screen.SetVram(address++, value2);
				written += 2;
			} while (--countHigh != 0);

			countLow = static_cast<uint8_t>(countLow - 1);
			if ((countLow & 0x80) != 0)
				break;
		}
	}
}
