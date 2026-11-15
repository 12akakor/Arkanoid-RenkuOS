/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "Game.h"
#include "VdpBlocks.h"
#include "TitleScreen.h"


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
	// A delay the cartridge spends inside its handler: the CPU is halted, so
	// nothing of the frame happens until it runs out, and what it was waiting
	// for happens on the frame it does.
	if (fDelay > 0) {
		if (--fDelay > 0)
			return;
		_ResumePending();
		return;
	}

	fInput.Update(keys, paddleCount, paddleStatus);
	fSoundPlayer.Tick();
	_UploadSprites();

	switch (fState.Read(GameRam::kTransitionAction)) {
		case 0: {
			// TRANSITION_START_LEVEL: the title screen, and the level the
			// game starts on.
			const TitleScreen::Result title = TitleScreen::Tick(fState, fScreen);
			if (title.playSound)
				fSoundPlayer.Play();
			if (title.frames > 0) {
				fDelay = static_cast<uint16_t>(title.frames);
				fPending = kPendingNone;
			}
			if (title.startLevel)
				_StartLevel();
			break;
		}
		case 1:
			_PlayLevel();
			break;
		case 2:
			_NextOrSameLevel();
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
	// still to come. The index is the step's own counter: it starts at zero,
	// counts every ball whether it is active or not, and is left holding three
	// when the loop ends (0x9872, 0x99C6).
	//
	// While every ball is inactive nothing moves, and the step is not run at
	// all: the cartridge leaves the index where it was - zero from the level
	// start - until a ball is launched (measured against it frame by frame).
	bool anyActive = false;
	for (int ball = 0; ball < GameRam::kBallCount; ball++) {
		if (fState.Read(static_cast<uint16_t>(GameRam::kBallTables
				+ ball * GameRam::kBallTableLength + GameRam::kActive)) != 0) {
			anyActive = true;
		}
	}

	if (anyActive) {
		fState.Write(GameRam::kBallLoopIndex, 0);
		for (int ball = 0; ball < GameRam::kBallCount; ball++) {
			const uint16_t table = static_cast<uint16_t>(GameRam::kBallTables
				+ ball * GameRam::kBallTableLength);
			if (fState.Read(table + GameRam::kActive) != 0) {
				fMotion.UpdatePosition(ball);

				// The last thing the original does with each ball (0x99AD):
				// one that has gone below the field is a lost ball. The test
				// sits in the loop, not in the movement, because an inactive
				// ball is left at 192 and the step is skipped for it at 0x9944
				// before this point.
				if (fState.Read(GameRam::kBallSprites
						+ ball * GameRam::kSpriteLength
						+ GameRam::kSpriteY) >= 184) {
					fMotion.BallState().OutBelow(ball, ball);
				}
			}
			fState.Write(GameRam::kBallLoopIndex,
				static_cast<uint8_t>(ball + 1));
		}
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
	// The original starts one entry below the parameters array (0xE0C9, the
	// slot the falling capsule uses) and tests thirty-two entries from there,
	// so the last sprite's parameters are never looked at.
	int slot = 0;
	for (int sprite = -1; sprite < kSpriteCount - 1; sprite++) {
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
// TRANSITION_START_LEVEL's target (0x4D09): the level's memory cleared, the
// screen cleared, the scores and "ROUND n" drawn, and the cartridge's 48-frame
// wait - unless the game is starting a level from a title, which waits for no
// one and goes straight on.
void
Game::_StartLevel()
{
	if (TitleScreen::StartLevel(fState, fScreen)) {
		fDelay = 48;
		fPending = kPendingStartLevel;
		return;
	}
	_LevelReloadDone();
}


// The tail of 0x4D09: the play level's action, the in-game graphics into the
// VDP, and the level itself.
void
Game::_LevelReloadDone()
{
	fState.Write(GameRam::kTransitionAction, 1);

	VdpBlocks::WritePatternTable(kRomImage + (GameRam::kInGameTiles - kRomBase),
		fScreen);
	VdpBlocks::FillColorsAllScreen(
		kRomImage + (GameRam::kInGameColors - kRomBase), fScreen);

	LoadLevel();
}


void
Game::LoadLevel()
{
	fLevels.ColorizeBackground();
	fLevels.WriteRoundMessage();
	fEffects.DrawLives();
	fLevels.CopyBrickMap();
	fLevels.AddBricksToTilemap();
	fLevels.DrawBackgroundTilemap();

	_ReadyForPlay();
}


// The level's last step, 0x4E78 to 0x4EDC: the Vaus and READY drawn as seven
// sprites over the attribute table, the level's music and its wait when a game
// is being played, and then the ball released - at BALL_X_DEMO's column - with
// IN_DEMO and the story's counters cleared and the sound put out.
void
Game::_ReadyForPlay()
{
	fScreen.CopyToVram(fScreen.SpriteAttributeBase(),
		kRomImage + (GameRam::kVausAndReadySprites - kRomBase), 7 * 4);

	fState.Write(GameRam::kSoundNumber, 0);
	fState.Write(static_cast<uint16_t>(GameRam::kInDemo), 0);
	fState.Write(static_cast<uint16_t>(GameRam::kInDemo + 1), 0);
	for (int i = 0; i < 6; i++)
		fState.Write(static_cast<uint16_t>(GameRam::kStoryVars + i), 0);

	// The demo starts at the title's side and waits for no music.
	if (fState.Read(GameRam::kGameState) == 0) {
		_ReleaseBall();
		return;
	}

	const bool finalLevel = fState.Read(GameRam::kLevel) == GameRam::kFinalLevel;
	fState.Write(GameRam::kSoundNumber, finalLevel
		? GameRam::kSoundDohLevelStarts : GameRam::kSoundLevelStart);
	fSoundPlayer.Play();

	fDelay = finalLevel ? 256 : 144;
	fPending = kPendingReady;
}


// The ball let go of the Vaus, and the one frame's wait after it.
void
Game::_ReleaseBall()
{
	fState.Write(GameRam::kBallTables + GameRam::kActive, 1);
	fState.Write(GameRam::kBallXDemo, 104);

	fDelay = 1;
	fPending = kPendingNone;
}


const uint8_t*
Game::Frame(bool spriteLimit)
{
	Renderer::Render(fScreen, fFrame, spriteLimit);
	return fFrame;
}


// NEXT_OR_SAME_LEVEL (0x7B94). Three actions, chosen by the transition type:
// the next level, the same level, or - both of the first two - the life that
// was lost. The title screen has no level to reload and goes straight to the
// tail.
void
Game::_NextOrSameLevel()
{
	if (fState.Read(GameRam::kGameState) == 0) {
		_BrickRepaintDone();
		return;
	}

	switch (fState.Read(GameRam::kLevelTransitionType)) {
		case GameRam::kLevelTransitionNext:
		case GameRam::kLevelTransitionNext2:
			_IncLevel();
			break;
		case GameRam::kLevelTransitionSame:
			_LifeLost();
			break;
		default:
			// TBL_ACTION_LEVEL has three entries; the cartridge would jump
			// into the table's neighbours.
			break;
	}
}


// ACTION_INC_LEVEL (0x7BB1). The displayed level is packed BCD and is
// incremented with DAA, the level itself in binary; the last level ends the
// game with the ending.
void
Game::_IncLevel()
{
	uint8_t display = static_cast<uint8_t>(
		fState.Read(GameRam::kLevelDisplay) + 1);
	if ((display & 0x0F) > 9)
		display = static_cast<uint8_t>(display + 0x06);
	if (display > 0x9F)
		display = static_cast<uint8_t>(display + 0x60);
	fState.Write(GameRam::kLevelDisplay, display);

	const uint8_t level
		= static_cast<uint8_t>(fState.Read(GameRam::kLevel) + 1);
	fState.Write(GameRam::kLevel, level);

	if (level != GameRam::kFinalLevel + 1) {
		fState.Write(GameRam::kTransitionAction, 0);
		fDelay = 60;
		return;
	}

	// The game is over: the levels are cleared, the ending plays. The ending
	// animation is not ported; the wait before it is.
	fState.Write(GameRam::kLevel, 0);
	fState.Write(GameRam::kLevelDisplay, 0);
	fDelay = 60;
}


// ACTION_LIFE_LOST (0x7C07). Forty-eight frames pass before the life goes,
// and what happens then depends on how many were left.
void
Game::_LifeLost()
{
	fDelay = 48;
	fPending = kPendingLifeLost;
}


void
Game::_LifeLostDone()
{
	const uint8_t lives
		= static_cast<uint8_t>(fState.Read(GameRam::kLives) - 1);
	fState.Write(GameRam::kLives, lives);

	if (lives != 0xFF) {
		// More lives: the game goes back to the title's transition, and waits
		// sixty frames before it runs.
		fState.Write(GameRam::kTransitionAction, 0);
		fDelay = 60;
		return;
	}

	// The last life. The level is kept for the cheat, the level shown as 32
	// when it was the last one.
	uint16_t level = static_cast<uint16_t>(fState.Read(GameRam::kLevel)
		| (fState.Read(GameRam::kLevelDisplay) << 8));
	if (fState.Read(GameRam::kLevel) == GameRam::kFinalLevel)
		level = 0x321F;
	fState.Write(GameRam::kCheat2Level, static_cast<uint8_t>(level & 0xFF));
	fState.Write(GameRam::kCheat2Level + 1, static_cast<uint8_t>(level >> 8));

	// GAME OVER, played at once rather than queued.
	fState.Write(GameRam::kSoundNumber, SoundCodes::kGameOver);
	fSoundPlayer.Play();

	// The four sprites that spell it, straight into the sprite table
	// (GAME_OVER_SPR_PARAMS at 0x7C5D).
	static const uint8_t kGameOver[4 * 4] = {
		0x88, 0x44, 0x70, 0x0F,
		0x88, 0x54, 0x74, 0x0F,
		0x88, 0x6C, 0x78, 0x0F,
		0x88, 0x7C, 0x7C, 0x0F
	};
	const uint16_t base = fScreen.SpriteAttributeBase();
	for (int i = 0; i < 4 * 4; i++)
		fScreen.SetVram(static_cast<uint16_t>(base + i), kGameOver[i]);

	fDelay = 240;
	fPending = kPendingGameOverClear;
}


void
Game::_ResumePending()
{
	const uint8_t pending = fPending;
	fPending = kPendingNone;
	switch (pending) {
		case kPendingStartLevel:
			_LevelReloadDone();
			break;
		case kPendingReady:
			_ReleaseBall();
			break;
		case kPendingLifeLost:
			_LifeLostDone();
			break;
		case kPendingGameOverClear:
			_ClearScreen();
			_BrickRepaintDone();
			break;
		default:
			break;
	}
}


// CLEAR_SCREEN (0x4227): the name table, the sprite table and the area the
// falling capsule's parameters live in, all blanked.
void
Game::_ClearScreen()
{
	VdpBlocks::ClearScreen(fState, fScreen);
}


// brick_repaint_action_done (0x7C44): the brick map wiped and the game back at
// the title.
void
Game::_BrickRepaintDone()
{
	for (uint16_t i = 0; i < GameRam::kBrickMapLength; i++)
		fState.Write(static_cast<uint16_t>(GameRam::kBrickMap + i), 0);

	fState.Write(GameRam::kTransitionAction, 0);
	fState.Write(GameRam::kGameState, 0);
	fState.Write(GameRam::kDohReceivedHits, 0);
}
