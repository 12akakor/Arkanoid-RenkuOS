/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The score display: the digits Arkanoid prints into its name table.
//
// The original keeps the scores as packed binary-coded decimal, unpacks them
// into ASCII with RRD and copies the result into video memory. Both halves are
// here, and the video memory is the port's Screen, so what the game draws can
// be compared with what the original draws.


#ifndef ARKANOID_HUD_H
#define ARKANOID_HUD_H


#include <stdint.h>

#include "Screen.h"
#include "State.h"


class Hud {
public:
						Hud(State& state, Screen& screen);

	// DRAW_SCORE_NUMBERS (0x53B9): both scores, at the place the game asks
	// for through SCORE_POSITION.
	void				DrawScoreNumbers();

	// DECODE_BCD_TO_ASCII (0x5420): six ASCII digits from three packed
	// bytes, most significant first. DE is the packed score in the
	// original, HL the buffer.
	static void			DecodeBcdToAscii(State& state, uint16_t buffer,
							uint16_t bcd);
	// REMOVE_HEADING_ZEROS (0x5431): the first five zeros become blanks,
	// so at least one digit is always left.
	static void			RemoveHeadingZeros(State& state, uint16_t buffer);

private:
	State&				fState;
	Screen&				fScreen;
};


#endif	// ARKANOID_HUD_H
