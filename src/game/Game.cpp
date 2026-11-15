/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "Game.h"


Game::Game(SoundPlayer::PsgSink* sink)
	:
	fState(),
	fScreen(),
	fInput(fState),
	fVaus(fState, fScreen),
	fMotion(fState, fScreen),
	fHud(fState, fScreen),
	fLevels(fState, fScreen),
	fEffects(fState, fScreen),
	fSound(fState),
	fSoundPlayer(fState, sink)
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

	// Every transition in the original jumps to GO_ON_AFTER_TRANSITION with
	// its work done, and that is where a queued sound becomes a sound.
	DrainSounds();
}


void
Game::DrainSounds()
{
	const uint8_t code = fSound.Pop();
	if (code == 0)
		return;

	fState.Write(GameRam::kSoundNumber, code);
	fSoundPlayer.Play();
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


// The sprite copy the original does every frame (0x4116 onwards): every sprite
// is parked out of sight in the shadow area, the entries that are worth showing
// are copied into it from the sprite parameters, and the whole area goes to the
// VDP in one go. The work is done in RAM first because that is what the
// original does, and a frame of the port has to leave the same memory behind.
//
// A sprite is shown only when it has a pattern number, a Y that is not the
// hidden one, and a colour; the original tests all three. It also swaps sprites
// 0 and 4 while a capsule falls, a workaround for the TMS9918A's four-sprites-
// per-line limit, which this renderer does not need.
void
Game::_UploadSprites()
{
	for (int sprite = 0; sprite < kSpriteCount; sprite++)
		fState.Write(GameRam::kSpriteAttribs + 4 * sprite + GameRam::kSpriteY, kHiddenY);

	// The entries that are shown are packed into the front of the area: the
	// original advances the area's pointer only where it copies, so a sprite
	// it skips does not take a slot.
	int slot = 0;
	for (int sprite = 0; sprite < kSpriteCount; sprite++) {
		const uint16_t source = static_cast<uint16_t>(kSpriteParams
			+ 4 * sprite);
		if (fState.Read(source + GameRam::kSpritePattern) == 0)
			continue;
		if (fState.Read(source + GameRam::kSpriteY) == kHiddenY)
			continue;
		if (fState.Read(source + GameRam::kSpriteColour) == 0)
			continue;

		for (int byte = 0; byte < 4; byte++) {
			fState.Write(static_cast<uint16_t>(GameRam::kSpriteAttribs
					+ 4 * slot + byte),
				fState.Read(static_cast<uint16_t>(source + byte)));
		}
		slot++;
	}

	const uint16_t base = fScreen.SpriteAttributeBase();
	for (int byte = 0; byte < 4 * kSpriteCount; byte++) {
		fScreen.SetVram(static_cast<uint16_t>(base + byte),
			fState.Read(static_cast<uint16_t>(GameRam::kSpriteAttribs + byte)));
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
