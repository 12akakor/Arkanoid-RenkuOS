/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The colour table's run-length compression, as the cartridge expands it.
#ifndef ARKANOID_TILE_COLORS_H
#define ARKANOID_TILE_COLORS_H


#include <stdint.h>


class Screen;


namespace TileColors {

// DECOMPRESS_TILE_COLORS (0x4389): expand `compressed` into the colour table
// from `vramAddress` for 0x800 bytes. Returns how many bytes were written.
int Decompress(const uint8_t* compressed, Screen& screen, uint16_t vramAddress);

}


#endif	// ARKANOID_TILE_COLORS_H
