/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Screen, see Screen.h.
#include "Screen.h"

#include <string.h>


Screen::Screen()
{
	Reset();
}


void
Screen::Reset()
{
	memset(fVram, 0, sizeof(fVram));
	memset(fRegisters, 0, sizeof(fRegisters));
}


void
Screen::SetRegister(int index, uint8_t value)
{
	fRegisters[index & 7] = value;
}


void
Screen::SetVram(uint16_t address, uint8_t value)
{
	fVram[address & (kVramSize - 1)] = value;
}


void
Screen::FillVram(uint16_t address, uint8_t value, int count)
{
	for (int i = 0; i < count; i++)
		fVram[(address + i) & (kVramSize - 1)] = value;
}


void
Screen::CopyToVram(uint16_t address, const uint8_t* source, int count)
{
	for (int i = 0; i < count; i++)
		fVram[(address + i) & (kVramSize - 1)] = source[i];
}
