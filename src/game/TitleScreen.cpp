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


// DRAW_ROUND_MESSAGE (0x5101): "ROUND " at 0x194C and the display level - one
// more than LEVEL_DISPLAY, in BCD - at 0x1952. A zero in the top digit leaves
// only the bottom one.
void
TitleScreen::DrawRoundMessage(State& state, Screen& screen)
{
	screen.CopyToVram(static_cast<uint16_t>(screen.NameBase() + 0x14C),
		Cart(GameRam::kRoundStr), 5);

	// add a,1 / daa, done a nibble at a time.
	uint8_t low = static_cast<uint8_t>((state.Read(GameRam::kLevelDisplay)
		& 0x0F) + 1);
	uint8_t high = static_cast<uint8_t>(state.Read(GameRam::kLevelDisplay) >> 4);
	if (low > 9) {
		low = 0;
		high++;
	}
	const uint8_t value = static_cast<uint8_t>((high << 4) | low);

	const uint16_t address = static_cast<uint16_t>(screen.NameBase() + 0x152);
	if (high != 0)
		screen.SetVram(address, static_cast<uint8_t>('0' + high));
	screen.SetVram(static_cast<uint16_t>(address + (high != 0 ? 1 : 0)),
		static_cast<uint8_t>('0' + low));
}


// TRANSITION_START_LEVEL's target, 0x4D09 to 0x4D46.
bool
TitleScreen::StartLevel(State& state, Screen& screen)
{
	if (state.Read(GameRam::kLevelTransitionType)
			!= GameRam::kLevelTransitionSame) {
		for (int i = 0; i < GameRam::kBrickMapLength; i++)
			state.Write(static_cast<uint16_t>(GameRam::kBrickMap + i), 0);
	} else {
		for (int i = 0; i < 0x4F5; i++)
			state.Write(static_cast<uint16_t>(GameRam::kControls + i), 0);
	}

	VdpBlocks::ClearScreen(state, screen);

	if (state.Read(GameRam::kGameState) == 0)
		return false;

	DrawUpScores(state, screen);
	DrawRoundMessage(state, screen);
	return true;
}


namespace {

// The waiting state, TITLE_SCREEN_ACTION_WAIT_IN_TITLE_SCREEN (0x4C48).
void
Wait(State& state, Screen& screen, TitleScreen::Result& result)
{
	// A paddle's button, or the space bar when there is no paddle.
	const bool pressed = state.Read(GameRam::kUseVausPaddle) != 0
		? (state.Read(static_cast<uint16_t>(GameRam::kPaddleStatus + 1))
			& 0x02) != 0
		: (state.Read(GameRam::kControls) & 0x10) != 0;

	if (pressed) {
		state.Write(GameRam::kBrickHitYPixel, 2);	// TITLE_SCREEN_ACTION_START_GAME
		screen.CopyToVram(static_cast<uint16_t>(screen.NameBase() + 0x1A8),
			Cart(GameRam::kGameStartStr), 17);
		state.Write(GameRam::kSoundNumber, GameRam::kSoundGameStartMusic);
		result.playSound = true;
		result.frames = 256;
		return;
	}

	const uint16_t ticks = static_cast<uint16_t>(state.Read(GameRam::kTitleTicks)
		| (state.Read(static_cast<uint16_t>(GameRam::kTitleTicks + 1)) << 8))
		+ 1;
	state.Write(GameRam::kTitleTicks, static_cast<uint8_t>(ticks & 0xFF));
	state.Write(static_cast<uint16_t>(GameRam::kTitleTicks + 1),
		static_cast<uint8_t>(ticks >> 8));

	// Ten seconds of no one pressing anything: the story, from the demo's side.
	if (ticks == 480) {
		state.Write(GameRam::kBrickHitYPixel, 5);	// TITLE_SCREEN_ACTION_DEMO
		state.Write(GameRam::kInDemo, 1);
		VdpBlocks::ClearScreen(state, screen);
	}
}


// TITLE_SCREEN_ACTION_START_GAME (0x4CC6): everything cleared, two lives - 240
// with the cheat - and the level the game starts on.
void
StartGame(State& state)
{
	state.Write(GameRam::kGameState, 1);

	// SCORE_BCD and the 1439 bytes after it.
	for (int i = 0; i < 1439; i++)
		state.Write(static_cast<uint16_t>(GameRam::kScoreBcd + i), 0);

	state.Write(GameRam::kNextLifeScore, 0x20);

	uint8_t lives = 2;
	if (state.Read(GameRam::kCheat1Activated) != 0)
		lives = 240;
	state.Write(GameRam::kLives, lives);

	state.Write(GameRam::kCheat1Activated, 0);
	state.Write(static_cast<uint16_t>(GameRam::kCheat1Activated + 1), 0);

	if (state.Read(GameRam::kCheat2Activated) != 0) {
		state.Write(GameRam::kLevel, state.Read(GameRam::kCheat2Level));
		state.Write(static_cast<uint16_t>(GameRam::kLevel + 1),
			state.Read(static_cast<uint16_t>(GameRam::kCheat2Level + 1)));
	}
	state.Write(GameRam::kCheat2Activated, 0);
	state.Write(static_cast<uint16_t>(GameRam::kCheat2Activated + 1), 0);
}


// TITLE_SCREEN_ACTION_GOTO_TITLE_SCREEN (0x4CA2): the demo's next level out of
// the four, and the game back at the title's side of the transition.
void
StartDemo(State& state)
{
	state.Write(GameRam::kGameState, 0);

	const uint8_t demo = static_cast<uint8_t>(
		(state.Read(GameRam::kDemoLevel) + 1) & 3);
	state.Write(GameRam::kDemoLevel, demo);
	state.Write(GameRam::kLevel, Cart(GameRam::kDemoLevelsTable)[demo]);
	state.Write(GameRam::kLevelTransitionType, 0);
}

}


// DRAW_TITLE_SCREEN (0x4B8A) as the game calls it on a frame.
TitleScreen::Result
TitleScreen::Tick(State& state, Screen& screen)
{
	Result result = { false, false, 0 };

	// A game in progress reloads a level (0x4D09) and the demo shows its story
	// (0x4EDD); neither is this screen's business.
	if (state.Read(GameRam::kGameState) != 0 || state.Read(GameRam::kInDemo) != 0)
		return result;

	switch (state.Read(GameRam::kBrickHitYPixel)) {
		case 1:
			Wait(state, screen, result);
			break;
		case 2:
			StartGame(state);
			result.startLevel = true;
			break;
		case 5:
			StartDemo(state);
			result.startLevel = true;
			break;
		default:
			Draw(state, screen);
			break;
	}
	return result;
}
