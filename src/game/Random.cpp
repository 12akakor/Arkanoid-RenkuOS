/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "Random.h"


Random::Random()
	:
	fState(0x1234),
	fFixed(false),
	fValue(0)
{
}


// A 16-bit linear congruential generator; only the high byte is used, which is
// the part that carries the randomness.
uint8_t
Random::Next()
{
	if (fFixed)
		return fValue;

	fState = static_cast<uint16_t>(fState * 25173u + 13849u);
	return static_cast<uint8_t>(fState >> 8);
}


void
Random::SetFixed(uint8_t value)
{
	fFixed = true;
	fValue = value;
}


void
Random::Release()
{
	fFixed = false;
}
