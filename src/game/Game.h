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
#include "Sound.h"
#include "State.h"
#include "Vaus.h"


class Game {
public:
	enum {
		kWidth = 256,
		kHeight = 192,
		kSpriteParams = 0xE0CD,		// SPR_PARAMS_BASE
		kSpriteCount = 32,
		kHiddenY = 192			// where a sprite is put to hide it (0x4116)
	};

	// The platform's sound output, if it has one. Every PSG write the driver
	// makes goes through this, and the driver reads register 7 back through
	// it when it takes the mixer over; NULL leaves it with its own register
	// image, which is what the tests use.
						explicit Game(SoundPlayer::PsgSink* sink = NULL);

	// GO_ON_AFTER_TRANSITION (0x41DA): a queued code becomes SOUND_NUMBER and
	// is played. The original reaches it from each of the three transitions,
	// so it runs every frame, after the transition's own work.
	void				DrainSounds();

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
			SoundPlayer&	GetSoundPlayer() { return fSoundPlayer; }

private:
	// NEXT_OR_SAME_LEVEL (0x7B94) and the two actions behind it. The delays
	// the cartridge spends inside the handler are `halt` loops there, forty
	// eight and sixty frames long; here they are frames in which nothing
	// happens, which is what the cartridge's memory does during them.
	void					_NextOrSameLevel();
	void					_IncLevel();
	void					_LifeLost();
	void					_LifeLostDone();
	void					_BrickRepaintDone();
	void					_ClearScreen();
	void					_ResumePending();

	enum {					// what to do when fDelay runs out
		kPendingNone = 0,
		kPendingLifeLost,
		kPendingGameOverClear
	};

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
			Sound			fSound;
			SoundPlayer		fSoundPlayer;
			uint8_t		fFrame[kWidth * kHeight];
			uint16_t		fDelay = 0;
			uint8_t			fPending = kPendingNone;
};


#endif	// ARKANOID_GAME_H
