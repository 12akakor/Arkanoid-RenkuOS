/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "Game.h"


Game::Game()
	:
	fState(),
	fScreen(),
	fInput(fState),
	fVaus(fState, fScreen),
	fMotion(fState, fScreen),
	fHud(fState, fScreen),
	fLevels(fState, fScreen),
	fEffects(fState, fScreen),
	fSoundPlayer(fState)
{
	fState.Clear();
	fScreen.Reset();
}


// VDP_HOOK_HANDLER (0x424A), kept to the parts that are ported. The sprites are
// put up first so that everything else this frame draws over the right ones.
void
Game::Tick(const KeyMatrix& keys, uint16_t paddleCount, uint8_t paddleStatus)
{
	fInput.Update(keys, paddleCount, paddleStatus);
	fSoundPlayer.Tick();
	_UploadSprites();

	switch (fState.Read(GameRam::kTransitionAction)) {
		case 0:
			// TRANSITION_START_LEVEL: the title screen and the story.
			// Not ported yet, so the field stays as it is.
			break;
		case 1:
			_PlayLevel();
			break;
		case 2:
			// TRANSITION_NEXT_LEVEL: NEXT_OR_SAME_LEVEL, which loads the
			// next level, takes a life or ends the game. Not ported yet.
			break;
		default:
			break;
	}
}


// TRANSITION_PLAY_LEVEL (0x41C0).
void
Game::_PlayLevel()
{
	fVaus.ExecuteAction();

	// UPDATE_OBJECTS (0x95F4), for the parts that are ported: three balls,
	// each one step, then the hard bricks' and the aliens' own updates are
	// still to come.
	for (int ball = 0; ball < GameRam::kBallCount; ball++) {
		if (fState.Read(GameRam::kBallTables
				+ ball * GameRam::kBallTableLength + GameRam::kActive) == 0)
			continue;
		fMotion.UpdatePosition(ball);
	}

	// The scores, drawn where the original draws them during play.
	fState.Write(GameRam::kScorePosition, 1);
	fHud.DrawScoreNumbers();
}


// The sprite copy the original does at the top of every tick: every sprite is
// first parked out of sight, then the shadow copy of its parameters is written
// into the sprite attribute table. The original also swaps sprites 0 and 4 while
// a capsule falls, which is a workaround for the TMS9918A's four-sprites-per-
// line limit; this renderer draws every sprite, so that step is not needed and
// is left out.
void
Game::_UploadSprites()
{
	const uint16_t base = fScreen.SpriteAttributeBase();
	for (int sprite = 0; sprite < kSpriteCount; sprite++)
		fScreen.SetVram(base + 4 * sprite, kHiddenY);

	for (int sprite = 0; sprite < kSpriteCount; sprite++) {
		const uint16_t source = kSpriteParams + 4 * sprite;
		for (int byte = 0; byte < 4; byte++) {
			fScreen.SetVram(base + 4 * sprite + byte,
				fState.Read(source + byte));
		}
	}
}


// The round's start, in the original's order (0x4DEB onwards): the colours, the
// round's number, the map, the field, and the field copied to video memory. The
// lives, the sprite patterns and the music that go in between are not ported.
void
Game::LoadLevel()
{
	fLevels.ColorizeBackground();
	fLevels.WriteRoundMessage();
	fEffects.DrawLives();
	fLevels.CopyBrickMap();
	fLevels.AddBricksToTilemap();
	fLevels.DrawBackgroundTilemap();
}


const uint8_t*
Game::Frame(bool spriteLimit)
{
	Renderer::Render(fScreen, fFrame, spriteLimit);
	return fFrame;
}
