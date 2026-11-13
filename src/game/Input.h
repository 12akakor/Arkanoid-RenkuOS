/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef ARKANOID_INPUT_H
#define ARKANOID_INPUT_H


#include <stdint.h>
#include <string.h>

#include "State.h"


class State;


// The keyboard matrix, as the platform sees it: 11 rows of 8 keys, true when
// the key is down. The original reads this through the PPI in its interrupt
// handler; on RenkuOS it comes from the window's key events.
struct KeyMatrix {
	bool	fKeys[11][8];

	void	Clear() { memset(fKeys, 0, sizeof(fKeys)); }
	void	Set(int row, int bit, bool down) { fKeys[row][bit] = down; }
};


// The frame's input, ported from the front of the original's interrupt
// handler (VDP_HOOK_HANDLER, 0x424A).
//
// The handler turns three matrix rows into one byte of active keys, works out
// which of them were pressed on this frame, runs the two cheat sequences and
// then either starts the game (SPACE at the title) or reads the paddle.
//
// The paddle is the one thing the port cannot take from the hardware: the
// original bit-bangs the PSG to read it, and on RenkuOS the position comes
// from the pointer or a joystick. So the platform passes the paddle status in,
// and the rest of the routine, which is what the game actually decides on, is
// the original's.
class Input {
public:
						Input(State& state);

	// One frame. paddleStatus is the value the original would have read
	// from the joystick port (bits 0-3 = the paddle's trigger line and
	// switch, bit 1 = the paddle button that starts the game).
	void				Update(const KeyMatrix& keys, uint16_t paddleCount,
							uint8_t paddleStatus);

	// Which keys were pressed on this frame rather than held, in the
	// original's layout: bit 0 up, 1 down, 2 left, 3 right, 4 SPACE,
	// 5 GRAPH, 6 STOP.
	uint8_t				Controls() const;

private:
	void				_CheckCheats(uint8_t controls);
	void				_StartFromKeyboard();
	void				_ReadPaddle(uint16_t paddleCount, uint8_t paddleStatus);

			State&		fState;
};


#endif	// ARKANOID_INPUT_H
