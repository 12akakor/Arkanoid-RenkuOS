/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "TitleScreen.h"

#include "RomImage.h"
#include "Screen.h"
#include "State.h"
#include "TileColors.h"
#include "VdpBlocks.h"


namespace {

// The cartridge's own memory, the way the Z80 reads it.
inline const uint8_t*
Cart(uint16_t address)
{
	return kRomImage + (address - kRomBase);
}


// DECODE_BCD_TO_ASCII (0x5420): three BCD bytes, most significant first from
// the top of the range, into six ASCII digits - high nibble first, because
// that is what RRD with A = 0x33 does.
void
DecodeBcdToAscii(State& state, uint16_t buffer, uint16_t bcd)
{
	uint16_t destination = buffer;
	uint16_t source = static_cast<uint16_t>(bcd + 2);

	for (int i = 0; i < 3; i++) {
		const uint8_t value = state.Read(source);
		state.Write(destination,
			static_cast<uint8_t>(0x30 | (value >> 4)));
		state.Write(static_cast<uint16_t>(destination + 1),
			static_cast<uint8_t>(0x30 | (value & 0x0F)));
		destination = static_cast<uint16_t>(destination + 2);
		source--;
	}
}


// REMOVE_HEADING_ZEROS (0x5431): five characters at most, every leading zero
// turned into a space, stopping at the first character that is not one.
void
RemoveHeadingZeros(State& state, uint16_t address)
{
	for (int i = 0; i < 5; i++) {
		if (state.Read(static_cast<uint16_t>(address + i)) != '0')
			return;
		state.Write(static_cast<uint16_t>(address + i), ' ');
	}
}

}


// DRAW_UP_SCORES (0x4FE0).
void
TitleScreen::DrawUpScores(State& state, Screen& screen)
{
	screen.CopyToVram(screen.NameBase(), Cart(GameRam::kScoreHiScoreStr), 21);

	// "0" under "SCORE" and under "HIGH SCORE", before the numbers replace them.
	screen.SetVram(static_cast<uint16_t>(screen.NameBase() + 7 + 1 * 32), '0');
	screen.SetVram(static_cast<uint16_t>(screen.NameBase() + 18 + 1 * 32), '0');

	state.Write(GameRam::kScorePosition, 0);
	DrawScoreNumbers(state, screen);
}


// DRAW_SCORE_NUMBERS (0x53B9). All three buffers are decoded and stripped of
// heading zeros, then the score and the high score are written - at the top of
// the screen, or down the right-hand side once the game is being played.
void
TitleScreen::DrawScoreNumbers(State& state, Screen& screen)
{
	DecodeBcdToAscii(state, GameRam::kDecodedAsciiScore, GameRam::kScoreBcd);
	RemoveHeadingZeros(state, GameRam::kDecodedAsciiScore);

	DecodeBcdToAscii(state, GameRam::kDecodedZeros, GameRam::kZerosBcdBuffer);
	RemoveHeadingZeros(state, GameRam::kDecodedZeros);

	DecodeBcdToAscii(state, GameRam::kDecodedAsciiHighScore,
		GameRam::kHighScoreBcd);
	RemoveHeadingZeros(state, GameRam::kDecodedAsciiHighScore);

	const uint16_t nameBase = screen.NameBase();
	if (state.Read(GameRam::kScorePosition) == 1) {
		screen.CopyToVram(static_cast<uint16_t>(nameBase + 25 + 7 * 32),
			state.Pointer(GameRam::kDecodedAsciiScore), 6);
		screen.CopyToVram(static_cast<uint16_t>(nameBase + 25 + 3 * 32),
			state.Pointer(GameRam::kDecodedAsciiHighScore), 6);
	} else {
		screen.CopyToVram(static_cast<uint16_t>(nameBase + 1 + 1 * 32),
			state.Pointer(GameRam::kDecodedAsciiScore), 6);
		screen.CopyToVram(static_cast<uint16_t>(nameBase + 12 + 1 * 32),
			state.Pointer(GameRam::kDecodedAsciiHighScore), 6);
	}
}


// The drawing half of DRAW_TITLE_SCREEN, 0x4BAA to 0x4C47.
void
TitleScreen::Draw(State& state, Screen& screen)
{
	// Border black - BDRCLR = 0 and CHGCLR, which writes it to register 7 - and
	// the screen cleared.
	state.Write(GameRam::kBorderColor, 0);
	screen.SetRegister(7, 0);
	VdpBlocks::ClearScreen(state, screen);

	// The title's patterns, three thirds from the one block.
	VdpBlocks::WritePatternTable(Cart(GameRam::kTitleTiles), screen);

	// Their colours.
	VdpBlocks::FillColorsAllScreen(Cart(GameRam::kTitleColors), screen);

	// The name table, then the scores at its top.
	screen.FillVram(screen.NameBase(), 0, 32 * 24);
	DrawUpScores(state, screen);

	// The Arkanoid logo, and the grid inside it.
	screen.CopyToVram(static_cast<uint16_t>(screen.NameBase() + 0xC0),
		Cart(GameRam::kArkanoidLogoChars), 0x60);

	const uint16_t sprite = GameRam::kSpriteParamsBase;
	state.Write(sprite, 52);							// y
	state.Write(static_cast<uint16_t>(sprite + 1), 148);	// x
	state.Write(static_cast<uint16_t>(sprite + 2), 0xA0);	// pattern
	state.Write(static_cast<uint16_t>(sprite + 3), 10);	// colour

	state.Write(GameRam::kBrickHitYPixel, 1);	// TITLE_SCREEN_ACTION_WAIT...

	// "PRESS START BUTTON", Taito's copyright and the reserved line. Then
	// Taito's logo, in two halves.
	screen.CopyToVram(static_cast<uint16_t>(screen.NameBase() + 0x1A8),
		Cart(GameRam::kPushStartButton), 17);
	screen.CopyToVram(static_cast<uint16_t>(screen.NameBase() + 0x284),
		Cart(GameRam::kTaitoCorp), 0x18);
	screen.CopyToVram(static_cast<uint16_t>(screen.NameBase() + 0x2C6),
		Cart(GameRam::kAllRightsReserved), 0x13);
	screen.CopyToVram(static_cast<uint16_t>(screen.NameBase() + 0x22B),
		Cart(GameRam::kTaitoHalf1), 0x0B);
	screen.CopyToVram(static_cast<uint16_t>(screen.NameBase() + 0x24B),
		Cart(GameRam::kTaitoHalf2), 0x0B);

	state.Write(GameRam::kTitleTicks, 0);
}
