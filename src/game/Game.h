/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The game's tick and the state machine it drives.
//
// The original does everything in its VBlank handler: read the input, put the
// sprites where they are, and then run one of three transitions - start a
// level, play it, or go on to the next one. This is that order, with the pieces
// that are ported wired together and the ones that are not named where they
// belong.
//
// What is ported: the input stage, Vaus, the ball's movement with the brick
// collision and the capsules, the score display, the aliens and Doh.
//
// What is not, yet: the title screen and the story (TRANSITION_START_LEVEL), the
// level loader behind NEXT_OR_SAME_LEVEL (TRANSITION_NEXT_LEVEL), and the PSG
// driver's per-tick step. Until those are ported the game can be played on the
// field it starts with, but it cannot start a level by itself.


#ifndef ARKANOID_GAME_H
#define ARKANOID_GAME_H


#include <stdint.h>

#include "BallMotion.h"
#include "BrickEffects.h"
#include "SoundPlayer.h"
#include "Hud.h"
#include "Levels.h"
#include "Input.h"
#include "Renderer.h"
#include "Screen.h"
#include "State.h"
#include "Vaus.h"


class Game {
public:
	enum {
		kWidth = 256,
		kHeight = 192,
		kSpriteParams = 0xE0CD,		// SPR_PARAMS_BASE
		kSpriteCount = 32,
		kHiddenY = 209			// where a sprite is put to hide it
	};

	// The platform's sound output, if it has one. Every PSG write the driver
	// makes goes through this, and the driver reads register 7 back through
	// it when it takes the mixer over; NULL leaves it with its own register
	// image, which is what the tests use.
						explicit Game(SoundPlayer::PsgSink* sink = NULL);

	// One frame, sixty times a second.
	void				Tick(const KeyMatrix& keys, uint16_t paddleCount,
							uint8_t paddleStatus);

	// The level's bricks and background, built and put on the screen. This is
	// the pair of calls the original makes as it starts a round
	// (ADD_BRICKS_TO_TILEMAP then DRAW_BACKGROUND_TILEMAP).
	void				LoadLevel();

	// The picture, one colour index per pixel, and its palette.
	const uint8_t*		Frame(bool spriteLimit = false);
	static uint32_t		Colour(int index) { return Renderer::Rgb(index); }

			Screen&		GetScreen() { return fScreen; }
			State&		GetState() { return fState; }

private:
	void				_UploadSprites();
	void				_PlayLevel();

			State		fState;
			Screen		fScreen;
			Input		fInput;
			Vaus		fVaus;
			BallMotion	fMotion;
			Hud			fHud;
			Levels		fLevels;
			BrickEffects	fEffects;
			SoundPlayer		fSoundPlayer;
			uint8_t		fFrame[kWidth * kHeight];
};


#endif	// ARKANOID_GAME_H
