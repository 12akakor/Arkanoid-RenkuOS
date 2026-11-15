/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#include "State.h"

#include <string.h>

#include "RomImage.h"


State::State()
{
	Clear();
}


void
State::Clear()
{
	memset(fRam, 0, sizeof(fRam));
}


uint8_t
State::Read(uint16_t address) const
{
	// The cartridge is there when the game reads it: tables are read from
	// the same bytes the Z80 fetches, at the same addresses.
	if (address >= kRomBase && address < kRomBase + kRomSize)
		return kRomImage[address - kRomBase];
	return fRam[address];
}


void
State::Write(uint16_t address, uint8_t value)
{
	fRam[address] = value;
	if (onWrite)
		onWrite(address, value);
}


const uint8_t*
State::Pointer(uint16_t address) const
{
	return &fRam[address];
}
