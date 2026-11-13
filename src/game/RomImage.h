/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The cartridge as data, so the game runs without a ROM file.
// Generated from build/arkanoid.rom, which tools/build_rom.sh verifies
// against the original (sha1 2183f07fa3ba87360100b2fa21fda0f55c0f8814).
// The game reads this image exactly where the original reads its cartridge,
// so every table keeps its address and its order.


#ifndef ARKANOID_ROM_IMAGE_H
#define ARKANOID_ROM_IMAGE_H

#include <stdint.h>


const int kRomSize = 0x8000;
const uint16_t kRomBase = 0x4000;

extern const uint8_t kRomImage[kRomSize];


#endif	// ARKANOID_ROM_IMAGE_H
