/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#include "Input.h"


namespace {

// Matrix positions, from the original's own three reads at 0x4250-0x4287.
enum {
	kRowGraph		= 6,
	kMatrixGraph	= 2,
	kRowStop		= 7,
	kMatrixStop		= 4,
	kRowCursor		= 8,
	kMatrixRight	= 7,
	kMatrixDown	= 6,
	kMatrixUp		= 5,
	kMatrixLeft	= 4,
	kMatrixSpace	= 0
};


// The key bits as the original packs them (`KEYBOARD_INPUT`): the three
// matrix reads are OR-ed, rotated and complemented into this layout, which is
// what the rest of the game tests. The test compares this against the
// original's own shifting, frame by frame.
enum {
	kBitUp		= 0x01,
	kBitDown	= 0x02,
	kBitLeft	= 0x04,
	kBitRight	= 0x08,
	kBitSpaceKey	= 0x10,
	kBitGraphKey	= 0x20,
	kBitStop	= 0x40
};


void
ClearPaddleHitArea(State& state)
{
	// `ld hl,BRICK_HIT_Y_PIXEL ... ldir` with BC = 7: eight bytes.
	for (int i = 0; i < 8; i++)
		state.Write(GameRam::kBrickHitYPixel + i, 0);
}

}	// namespace


Input::Input(State& state)
	:
	fState(state)
{
}


// VDP_HOOK_HANDLER, the input part (0x424A), which is the whole front of the
// frame: the keys, then what to do with them.
void
Input::Update(const KeyMatrix& keys, uint16_t paddleCount,
	uint8_t paddleStatus)
{
	const bool* cursor = keys.fKeys[kRowCursor];

	uint8_t active = 0;
	if (cursor[kMatrixUp])
		active |= kBitUp;
	if (cursor[kMatrixDown])
		active |= kBitDown;
	if (cursor[kMatrixLeft])
		active |= kBitLeft;
	if (cursor[kMatrixRight])
		active |= kBitRight;
	if (cursor[kMatrixSpace])
		active |= kBitSpaceKey;
	if (keys.fKeys[kRowGraph][kMatrixGraph])
		active |= kBitGraphKey;
	if (keys.fKeys[kRowStop][kMatrixStop])
		active |= kBitStop;

	// CONTROLS: the keys pressed on this frame rather than held. Only the
	// top four bits are edge-detected (`and 0f0h`), the rest pass through.
	const uint8_t previous = fState.Read(GameRam::kKeyboardInput);
	fState.Write(GameRam::kKeyboardInput, active);
	const uint8_t controls = static_cast<uint8_t>(
		((previous & 0xf0) & active) ^ active);
	fState.Write(GameRam::kControls, controls);

	// During play the keys are not looked at here: the Vaus reads CONTROLS
	// itself.
	if (fState.Read(GameRam::kGameState) != 0) {
		_ReadPaddle(paddleCount, paddleStatus);
		return;
	}

	_CheckCheats(controls);

	// START goes back to the cursor keys and returns; only without it does
	// the paddle get read, which is why its counters are untouched then.
	if ((controls & kBitSpaceKey) != 0)
		_StartFromKeyboard();
	else
		_ReadPaddle(paddleCount, paddleStatus);
}


uint8_t
Input::Controls() const
{
	return fState.Read(GameRam::kControls);
}


// The two cheat sequences. Up, down and GRAPH four times over activates the
// first; left, right and GRAPH four times over the second (continue from the
// last level).
void
Input::_CheckCheats(uint8_t controls)
{
	if ((controls & kBitUp) != 0 && (controls & kBitDown) != 0
		&& (controls & kBitGraphKey) != 0) {
		const uint8_t count = fState.Read(GameRam::kCheat1Counter) + 1;
		if (count < 4)
			fState.Write(GameRam::kCheat1Counter, count);
		else {
			fState.Write(GameRam::kCheat1Counter, 0);
			fState.Write(GameRam::kCheat1Activated, 1);
		}
	}

	if ((controls & kBitLeft) != 0 && (controls & kBitRight) != 0
		&& (controls & kBitGraphKey) != 0) {
		const uint8_t count = fState.Read(GameRam::kCheat2Counter) + 1;
		if (count < 4)
			fState.Write(GameRam::kCheat2Counter, count);
		else {
			fState.Write(GameRam::kCheat2Counter, 0);
			fState.Write(GameRam::kCheat2Activated, 1);
		}
	}
}


// START pressed at the title with the keyboard: back to the cursor keys, and
// a level transition queued.
void
Input::_StartFromKeyboard()
{
	if (fState.Read(GameRam::kTransitionAction) != 0) {
		fState.Write(GameRam::kTransitionAction, 0);
		ClearPaddleHitArea(fState);
	}

	// Cursor keys, not the paddle.
	fState.Write(GameRam::kUseVausPaddle, 0);
}


// The paddle branch. The original reads a 9-bit count and the trigger line
// from the PSG here; the platform supplies both, and what the game decides on
// it - which is the part that matters - is the original's code.
void
Input::_ReadPaddle(uint16_t paddleCount, uint8_t paddleStatus)
{
	fState.Write(GameRam::kPaddleCount,
		static_cast<uint8_t>(paddleCount & 0xff));
	fState.Write(GameRam::kPaddleCount + 1,
		static_cast<uint8_t>((paddleCount >> 8) & 0x01));

	const uint8_t previous = fState.Read(GameRam::kPaddleStatus);
	fState.Write(GameRam::kPaddleStatus, paddleStatus);

	const uint8_t pressed = static_cast<uint8_t>(
		((previous & 0x0f) & paddleStatus) ^ paddleStatus);
	fState.Write(GameRam::kPaddleStatus + 1, pressed);

	if (fState.Read(GameRam::kGameState) != 0)
		return;

	// Bit 1 is the paddle's button.
	if ((pressed & 0x02) == 0)
		return;

	if (fState.Read(GameRam::kTransitionAction) != 0) {
		fState.Write(GameRam::kTransitionAction, 0);
		ClearPaddleHitArea(fState);
	}

	fState.Write(GameRam::kUseVausPaddle, 1);
}
