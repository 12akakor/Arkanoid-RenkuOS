/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// What DRAW_TITLE_SCREEN draws, and the scores it draws with it.
#ifndef ARKANOID_TITLE_SCREEN_H
#define ARKANOID_TITLE_SCREEN_H


class Screen;
class State;


namespace TitleScreen {

// DRAW_UP_SCORES (0x4FE0): "SCORE    HIGH SCORE", two zeros under it, and the
// numbers themselves.
void DrawUpScores(State& state, Screen& screen);

// DRAW_SCORE_NUMBERS (0x53B9): the score and the high score decoded out of BCD
// and put where SCORE_POSITION says.
void DrawScoreNumbers(State& state, Screen& screen);

// The drawing half of DRAW_TITLE_SCREEN (0x4BAA to 0x4C47): the screen cleared,
// the title's patterns and colours in, the logo, the strings, Taito's logo, the
// sprite in the logo's grid, and the screen waiting for START.
void Draw(State& state, Screen& screen);

// DRAW_ROUND_MESSAGE (0x5101): "ROUND " and the level number, as the level
// start draws it.
void DrawRoundMessage(State& state, Screen& screen);

// DRAW_TITLE_SCREEN's whole dispatch (0x4B8A): what the title screen does on a
// frame, whichever of its states it is in.
struct Result {
	bool	startLevel;	// the level reload at 0x4D09 has to run
	bool	playSound;	// SOUND_NUMBER wants playing
	int		frames;		// a wait this routine spends inside itself
};

Result Tick(State& state, Screen& screen);

// The level reload's own start (0x4D09, up to the wait): the level's memory
// cleared - or the input block, when the level stays as it is - the screen
// cleared, and the scores and "ROUND n" drawn when a game is being played.
// Returns true when the cartridge's 48-frame wait comes next.
bool StartLevel(State& state, Screen& screen);

// The story, 0x4EDD: the text typed a character every two frames, a 120-frame
// pause to read it, and START skipping straight out of it.
void Story(State& state, Screen& screen);

}


#endif	// ARKANOID_TITLE_SCREEN_H
