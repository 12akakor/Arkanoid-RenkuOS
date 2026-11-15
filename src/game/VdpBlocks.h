/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The block copies the title screen and every level start make into the VDP.
#ifndef ARKANOID_VDP_BLOCKS_H
#define ARKANOID_VDP_BLOCKS_H


#include <stdint.h>


class Screen;
class State;


namespace VdpBlocks {

// LDIRVM_32x24_THIRD (0x4220): one third of the pattern table, 2048 bytes,
// straight from the cartridge to VRAM.
void WriteThird(const uint8_t* source, Screen& screen, uint16_t vramAddress);

// The three calls that fill the pattern table from the same data.
void WritePatternTable(const uint8_t* source, Screen& screen);

// CLEAR_SCREEN (0x4227): the name table and the sprite attributes blanked, and
// the pocket of memory at 0xE0C9 that the game's copies of them live in.
void ClearScreen(State& state, Screen& screen);

// FILL_COLORS_ALL_SCREEN (0x41FF): the title's compressed colours expanded
// into the three thirds of the colour table.
void FillColorsAllScreen(const uint8_t* compressed, Screen& screen);

}


#endif	// ARKANOID_VDP_BLOCKS_H
