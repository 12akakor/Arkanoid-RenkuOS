/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "VdpBlocks.h"

#include "Screen.h"
#include "TileColors.h"


namespace {

// 32 * 24 * 8 / 3: one third of the 768 tiles' patterns.
const uint16_t kThird = 2048;
const uint16_t kPatternTable = 0x0000;
const uint16_t kColorTable = 0x2000;


void
Write(const uint8_t* source, Screen& screen, uint16_t vramAddress, int length)
{
	for (int i = 0; i < length; i++)
		screen.SetVram(static_cast<uint16_t>(vramAddress + i), source[i]);
}

}


// LDIRVM_32x24_THIRD (0x4220): BC = 32*24*8/3 and LDIRVM, so the source is the
// cartridge's own memory - the routine is handed an address in the cartridge
// and reads it there.
void
VdpBlocks::WriteThird(const uint8_t* source, Screen& screen, uint16_t vramAddress)
{
	Write(source, screen, vramAddress, kThird);
}


// The three calls the title screen and the level loader make, all from the same
// data, one third of the pattern table each.
void
VdpBlocks::WritePatternTable(const uint8_t* source, Screen& screen)
{
	for (int third = 0; third < 3; third++) {
		WriteThird(source, screen,
			static_cast<uint16_t>(kPatternTable + third * kThird));
	}
}


// FILL_COLORS_ALL_SCREEN (0x41FF): TITLE_COLORS_COMPRESSED decompressed three
// times, once per third of the colour table.
void
VdpBlocks::FillColorsAllScreen(const uint8_t* compressed, Screen& screen)
{
	for (int third = 0; third < 3; third++) {
		TileColors::Decompress(compressed, screen,
			static_cast<uint16_t>(kColorTable + third * kThird));
	}
}
