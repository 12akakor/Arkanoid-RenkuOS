/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The port's random source.
//
// The original reads the Z80's memory refresh register where it wants a random
// number, adding whatever the registers happened to hold at the time. That
// value is a property of the machine's timing, not of the game: the same game
// state gives a different capsule on different runs. Reproducing it is neither
// possible nor meaningful, so the port keeps the same distribution - any byte -
// and replaces only the source.
//
// Everything the game does with the number is kept exactly: the 1-in-33 gap
// between capsules, the 1-in-50 gap between lives, the tables and the
// arithmetic around them.
//
// A fixed value can be supplied instead, which is how the tests drive the code
// that uses it.


#ifndef ARKANOID_RANDOM_H
#define ARKANOID_RANDOM_H


#include <stdint.h>


class Random {
public:
						Random();

	// The next byte. With a fixed value set, that value, unchanged.
	uint8_t				Next();

	// Makes Next() return this value every time.
	void				SetFixed(uint8_t value);
	void				Release();

private:
	uint16_t			fState;
	bool				fFixed;
	uint8_t				fValue;
};


#endif	// ARKANOID_RANDOM_H
