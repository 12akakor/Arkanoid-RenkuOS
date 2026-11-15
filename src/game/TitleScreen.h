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

}


#endif	// ARKANOID_TITLE_SCREEN_H
