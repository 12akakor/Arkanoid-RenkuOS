/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "Hud.h"


Hud::Hud(State& state, Screen& screen)
	:
	fState(state),
	fScreen(screen)
{
}


// DECODE_BCD_TO_ASCII (0x5420). Three packed bytes hold six digits, most
// significant byte last, so the walk starts there and goes down. Each byte is
// split with the Z80's RRD: the high nibble becomes the first character and
// the low nibble the second, both offset by 0x30 to make ASCII.
void
Hud::DecodeBcdToAscii(State& state, uint16_t buffer, uint16_t bcd)
{
	uint16_t source = static_cast<uint16_t>(bcd + 2);
	for (int i = 0; i < 3; i++) {
		const uint8_t packed = state.Read(source);
		state.Write(buffer++, static_cast<uint8_t>((packed >> 4) | 0x30));
		state.Write(buffer++, static_cast<uint8_t>((packed & 0x0f) | 0x30));
		source--;
	}
}


// REMOVE_HEADING_ZEROS (0x5431). Five tries at most: a score of zero still
// shows one digit.
void
Hud::RemoveHeadingZeros(State& state, uint16_t buffer)
{
	for (int i = 0; i < 5; i++) {
		if (state.Read(buffer) != '0')
			return;
		state.Write(buffer++, ' ');
	}
}


// DRAW_SCORE_NUMBERS (0x53B9). The score, the zeros buffer and the high score
// are decoded, then six characters of each are copied into the name table.
// The original writes to 0x1800 because that is its name table; the port asks
// its Screen for the same base, which the game's registers set to 0x1800.
void
Hud::DrawScoreNumbers()
{
	DecodeBcdToAscii(fState, GameRam::kDecodedAsciiScore,
		GameRam::kScoreBcd);
	RemoveHeadingZeros(fState, GameRam::kDecodedAsciiScore);

	DecodeBcdToAscii(fState, GameRam::kDecodedZeros,
		GameRam::kZerosBcdBuffer);
	RemoveHeadingZeros(fState, GameRam::kDecodedZeros);

	DecodeBcdToAscii(fState, GameRam::kDecodedAsciiHighScore,
		GameRam::kHighScoreBcd);
	RemoveHeadingZeros(fState, GameRam::kDecodedAsciiHighScore);

	const uint16_t name = fScreen.NameBase();
	if (fState.Read(GameRam::kScorePosition) == 1) {
		// Scores on the right, with the high score above the score.
		fScreen.CopyToVram(static_cast<uint16_t>(name + 25 + 7 * 32),
			&fState.Ram()[GameRam::kDecodedAsciiScore], 6);
		fScreen.CopyToVram(static_cast<uint16_t>(name + 25 + 3 * 32),
			&fState.Ram()[GameRam::kDecodedAsciiHighScore], 6);
	} else {
		// Scores along the top, high score on the right.
		fScreen.CopyToVram(static_cast<uint16_t>(name + 1 + 1 * 32),
			&fState.Ram()[GameRam::kDecodedAsciiScore], 6);
		fScreen.CopyToVram(static_cast<uint16_t>(name + 12 + 1 * 32),
			&fState.Ram()[GameRam::kDecodedAsciiHighScore], 6);
	}
}
