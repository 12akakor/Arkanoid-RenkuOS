/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */
#ifndef ARKANOID_STATE_H
#define ARKANOID_STATE_H


// The game's memory: one 64 KB array, addressed exactly as the Z80 addresses
// it. The original keeps its whole state in RAM at fixed addresses and passes
// pointers to it around, so keeping the addresses is what makes the port
// checkable against the original, line by line and routine by routine.
//
// Cartridge addresses (0x4000-0xBFFF) read from the built-in ROM image, the
// same bytes the Z80 fetches, so tables are read from the cartridge rather
// than copied: a table's address in the disassembly is its address here.
#include <stdint.h>

#include <functional>

#include "RomImage.h"


namespace GameRam {

// Variables the ball code uses. Named after the disassembly's labels.
enum {
	kTransitionAction		= 0xE00A,
	kGameState				= 0xE00B,
	kUseVausPaddle			= 0xE00C,
	kLevel					= 0xE01B,
	kLevelDisplay			= 0xE01C,	// LEVEL_DISP, packed BCD
	kBrickMap				= 0xE027,	// BRICK_MAP
	kBrickMapLength			= 0x58D,
	kLives					= 0xE01D,
	kBricksLeft				= 0xE038,
	kControls				= 0xE0BF,
	kKeyboardInput			= 0xE0C0,
	kPaddleCount			= 0xE0C1,
	kPaddleStatus			= 0xE0C4,
	kCheat1Activated		= 0xE001,
	kCheat1Counter			= 0xE002,
	kCheat2Level			= 0xE005,	// CHEAT2_LEVEL (two bytes)
	kCheat2Activated		= 0xE003,
	kCheat2Counter			= 0xE004,
	kBrickHitYPixel			= 0xE53C,
	kBrickHitXPixel			= 0xE53D,
	kVausX2					= 0xE53E,
	kTicksToHit				= 0xE541,
	kBallXSlope				= 0xE542,
	kBallYSlope				= 0xE543,
	kPrevYPixel				= 0xE586,
	kPrevXPixel				= 0xE587,
	kCurrBrickY				= 0xE58A,
	kCurrBrickX				= 0xE58B,
	kPrevBrickY				= 0xE58C,
	kPrevBrickX				= 0xE58D,
	kHiyYEdgeA				= 0xE2C4,
	kHiyYEdgeB				= 0xE2C5,
	kComputedHitXNeg		= 0xE2C6,
	kComputedHitX			= 0xE2C7,
	kBrickRow				= 0xE2AA,
	kBrickCol				= 0xE2AB,
	kSpriteParamsBase		= 0xE0CD,
	kSpriteAttribs			= 0xE18D,	// SPRITE_ATTRIBS_AREA
	kVausX					= 0xE0CE,	// Vaus left edge
	kBallBouncesCounter		= 0xE51C,
	kDohBeenHit				= 0xE2B9,
	kExtraBalls				= 0xE325,
	kGluingStatus			= 0xE324,
	kLasersFiring			= 0xE519,
	kSoundsCount			= 0xE51E,
	kSoundsBuffer			= 0xE520,
	kSoundNumber			= 0xE5C0,
	kCanAddSound			= 0xE5C2,
	kVausActionState		= 0xE54B,
	kVausResizing			= 0xE550,

	// The scores, packed binary-coded decimal, and the ASCII buffers the
	// game decodes them into before drawing them.
	kHighScoreBcd			= 0xE007,	// HIGH_SCORE_BCD
	kScoreBcd				= 0xE015,	// SCORE_BCD
	kZerosBcdBuffer		= 0xE018,	// ZEROS_BCD_BUFFER
	kScorePosition			= 0xE544,	// SCORE_POSITION
	kDecodedAsciiScore	= 0xE58E,	// DECODED_ASCII_SCORE
	kDecodedZeros			= 0xE594,	// DECODED_ZEROS
	kDecodedAsciiHighScore	= 0xE59A	// DECODED_ASCII_HIGH_SCORE
};


// The three balls: 20 bytes each, one after the other.
enum {
	kBallTables				= 0xE24E,	// BALL_TABLE1
	kBallTableLength		= 20,
	kBallCount				= 3,
	kBallLoopIndex			= 0xE2AC	// BALL_LOOP_INDEX
};


// Fields inside a ball's table (BALL_TABLE_IDX_*).
enum {
	kActive					= 0x00,
	kGlue					= 0x01,
	kYSpeed					= 0x02,
	kXSpeed					= 0x03,
	kMoveCounter			= 0x05,
	kSkewness				= 0x06,
	kSpeedPosition			= 0x07,
	kSpeedCounter			= 0x0D,
	kSpeedMultiplier		= 0x08,
	kMoveTarget				= 0x09,
	kGlueCounter			= 0x0E,
	kVausHitX				= 0x10
};


// The sprites: 4 bytes each (Y, X, pattern, colour), one per ball.
enum {
	kBallSprites			= 0xE0F5,	// BALL1_SPR_PARAMS
	kSpriteLength			= 4,
	kSpriteY				= 0x00,
	kSpriteX				= 0x01,
	kSpritePattern			= 0x02,
	kSpriteColour			= 0x03
};


// Values compared against by the ball code.
enum {
	kFinalLevel				= 0x20,
	kActionStateEnlarging	= 0x02,
	kActionStateExploding	= 0x06,
	kActionStatePortal		= 0x07,
	kGluingStateSticky		= 0x01
};


// Sound codes passed to ADD_SOUND.
enum {
	kSoundBallOnVaus		= 0x01,
	kSoundGluedBall			= 0x04,
	kSoundVausDestroyed		= 0x07
};


// Cartridge tables, at their disassembly addresses.
enum {
	kSpeedPerSkewnessPtr	= 0x9A98,	// TBL_PTR_BALL_SPEED_PER_SKEWNESS_PTR
	kSkewnessPosToXY		= 0x9A78,	// TBL_SKEWNESS_POS_TO_XY_SPEED
	kSkewnessNegToXY		= 0x9A88,	// TBL_SKEWNESS_NEG_TO_XY_SPEED
	kVausSkewnessTable		= 0x9C27,	// BALL_SKEWNESS_TABLE
	kCyclicSkewnessTable	= 0xAB6A,	// TBL_SKEWNESS
	kBallSpeedTable			= 0x9B1A,	// BALL_SPEED_TABLE
	kSpeedFromSkewness		= 0xA86C,	// TBL_SPEED_FROM_SKEWNESS
	kTableBackgroundErase	= 0xAD90,	// TABLE_BACKGROUND_ERASE
	kTblBrickActions		= 0xAA52,	// TBL_BRICK_ACTIONS
	kTblBrickActionOffsets	= 0xAA5C,	// TBL_BRICK_ACTION_TABLE_OFFSETs
	kBrickActionTableOffset	= 0xE2BC,	// the level's table, once known
	kBackgroundTilemap		= 0xE36E,	// BACKGROUND_TILEMAP
	kBrickUnused1			= 0xE2BA,	// BRICK_UNUSED_1
	kBrickUnused2			= 0xE2BB,	// BRICK_UNUSED_2
	kHardBricksRemainingHits = 0xE039,	// HARD_BRICKS_REMAINING_HITS
	kActionSkewnessCounter	= 0xE5AC,	// ACTION_SKEWNESS_COUNTER
	// The tables the title screen reads out of the cartridge (0x4B8A).
	kTitleTiles				= 0x9024,	// TITLE_TILES
	kTitleColors			= 0x93F4,	// TITLE_COLORS_COMPRESSED
	kArkanoidLogoChars		= 0x543E,	// ARKANOID_LOGO_CHARS
	kPushStartButton		= 0x54B3,	// PUSH_START_BUTTON_STR
	kTaitoCorp				= 0x54DE,	// TAITO_CORP_STR
	kAllRightsReserved		= 0x54F6,	// ALL_RIGHTS_RESERVED_STR
	kTaitoHalf1				= 0x5509,	// TAITO_HALF1_LOGO_CHARS
	kTaitoHalf2				= 0x5514,	// TAITO_HALF2_LOGO_CHARS
	kLevelTransitionType	= 0xE022,	// LEVEL_TRANSITION_TYPE
	kScoreLifeBcd			= 0xE01E,	// SCORE_LIFE_BCD
	kScoreBcdBuffer			= 0xE5A0,	// SCORE_BCD_BUFFER
	kHardBrickTable			= 0xE20D,	// HARD_BRICK_TABLE
	kPointsTable			= 0x5363,	// POINTS_TABLE
	kTblPointsPerBrick		= 0x9677,	// TBL_POINTS_PER_BRICK
	kLivesVdpAddresses		= 0x71F8,	// LIVES_VDP_ADDRESSES
	kLivePatterns			= 0x71F6,	// LIVE_PATTERNS
	kCapsulesLeft			= 0xE023,	// CAPSULES_LEFT
	kCapsulesRandomNum		= 0xE024,	// CAPSULES_RANDOM_NUM
	kCapsuleLivesCounterL	= 0xE025,	// CAPSULE_LIVES_COUNTER_L
	kCapsuleLivesCounterH	= 0xE026,	// CAPSULE_LIVES_COUNTER_H
	kCapsuleIsFalling		= 0xE317,	// CAPSULE_IS_FALLING
	kCapsuleType			= 0xE318,	// CAPSULE_TYPE
	kLifeObtainedFlag		= 0xE327,	// LIFE_OBTAINED_FLAG
	kVausIsEnlarged			= 0xE321,	// VAUS_IS_ENLARGED
	kVausHasLasers			= 0xE322,	// VAUS_HAS_LASERS
	kFallingCapsuleSprite	= 0xE0C9,	// FALLING_CAPSULE_SPR_PARAMS

	// The aliens: three entries of twenty bytes, and their sprites.
	kAlienTable				= 0xE4C7,	// ALIEN_TABLE
	kAlienTableLength		= 20,		// ALIEN_TABLE_LEN
	kAlienSpriteParams		= 0xE101,	// ALIEN_SPR_PARAMS
	kSpriteParamsLength		= 4,		// SPR_PARAMS_LEN
	kAlienColour			= 0,		// ALIEN_TABLE_IDX_COLOR
	kAlienActive			= 1,		// ALIEN_TABLE_IDX_ACTIVE
	kAlienActiveExploding	= 2,		// ALIEN_ACTIVE_EXPLODING
	kAlienExplodingFlag		= 1,		// ALIEN_EXPLODING_FLAG

	// Doh, at the end of the game.
	kTblDohHit				= 0xE505,	// TBL_DOH_HIT
	kDohTable				= 0xE50D,	// DOH_TABLE
	kDohReceivedHits		= 0xE5B3,	// DOH_RECEIVED_HITS

	// Vaus, its controls and the two cartridge tables its code reads.
	kVausTable				= 0xE54B,	// VAUS_TABLE
	kVausIdxActionState		= 0,		// VAUS_TABLE_IDX_ACTION_STATE
	kVausIdxResizingStep	= 1,		// VAUS_TABLE_IDX_RESIZING_STEP
	kVausIdxLaseringStep	= 2,		// VAUS_TABLE_IDX_LASERING_STEP
	kVausIdxDestructionStep1 = 3,		// VAUS_TABLE_IDX_DESTRUCTION_STEP1
	kVausIdxDestructionStep2 = 4,		// VAUS_TABLE_IDX_DESTRUCTION_STEP2
	kVausIdxResizing		= 5,		// VAUS_TABLE_IDX_RESIZING
	kVausIdxHasLaser		= 6,		// VAUS_TABLE_IDX_HAS_LASER
	kVausActionStateWait	= 0,		// VAUS_ACTION_STATE_WAIT_READY
	kVausActionStateKeep	= 1,		// VAUS_ACTION_STATE_KEEP
	kVausOldX				= 0xE54A,	// the byte before VAUS_X
	kBallXDemo				= 0xE0F6,	// BALL_X_DEMO
	kResetVausSpritePosition = 0xE5A9,	// RESET_VAUS_SPR_POSITION
	kIncrementPosVausControls = 0x6FF7,	// TBL_INCREMENT_POS_VAUS_CONTROLS
	kArkanoidCenteredSprites = 0x6FE7,	// SPR_DATA_ARKANOID_CENTERED
	kPortalOpen				= 0xE326,	// PORTAL_OPEN
	kPortalAlreadyDrawnOpen	= 0xE328,	// PORTAL_ALREADY_DRAWN_OPEN
	kPortalBeamCounter		= 0xE329,	// PORTAL_ANIMATION_BEAM_COUNTER
	kPortalBeamStep			= 0xE32A,	// PORTAL_ANIMATION_BEAM_STEP
	kOpenPortalPatterns		= 0x68B6,	// OPEN_PORTAL_PATTERNS
	kPortalBeamPatterns		= 0x68BA,	// PORTAL_BEAM_PATTERNS
	kSoundVausThroughPortal = 193,		// SOUND_VAUS_GOES_THROUGH_PORTAL_H

	// The sounds these two make.
	// The level loader's tables.
	kLevelsPtrTable			= 0x5DEF,	// LEVELS_PTR_TABLE, 32 words
	kLevelColorsPtrTable	= 0x5E2F,	// LEVEL_COLORS_PTR_TABLE
	kBricksPerLevel			= 0x5D00,	// BRICKS_PER_LEVEL
	kTblColorToPattern		= 0x5DDB,	// TBL_COLOR_TO_PATTERN
	kTableBackgroundEntry1	= 0xAD98,	// TABLE_BACKGROUND_ENTRY1
	kLevelTransitionNext	= 0,		// LEVEL_TRANSITION_NEXT
	kLevelTransitionNext2	= 1,		// LEVEL_TRANSITION_NEXT2
	kLevelTransitionSame	= 2,		// LEVEL_TRANSITION_SAME
	kTilemapVramAddress		= 0x1862,	// where DRAW_BACKGROUND_TILEMAP puts it
	kTilemapWidth			= 22,
	kTilemapRows			= 12,
	kTilemapStride			= 32,

	kLevelDispBcd			= 0xE01C,	// LEVEL_DISP
	kBackgroundColours		= 0x5BEB,	// BACKGROUND_COLORS_TABLE
	kColourTableVram		= 0x2380,	// the VDP colour table
	kRoundString			= 0x723C,	// ROUND_STR2

	kSoundDohHit			= 8,		// SOUND_DOH_HIT
	kSoundDohDefeated		= 9,		// SOUND_DOH_DEFEATED
	kSoundAlienDestroyed	= 194		// SOUND_ALIEN_DESTROYED
};

}	// namespace GameRam


class State {
public:
						State();

	void				Clear();

	// Reads take the cartridge image into account, so a table at 0x9A98
	// is the table at 0x9A98.
	uint8_t				Read(uint16_t address) const;
	void				Write(uint16_t address, uint8_t value);

	uint8_t*			Ram() { return fRam; }
	const uint8_t*		Ram() const { return fRam; }

	// Every write, in order, for the differential tests: the port's write
	// sequence can then be compared with the original's instruction by
	// instruction instead of only at the end of a tick.
	std::function<void(uint16_t address, uint8_t value)> onWrite;

private:
	uint8_t				fRam[0x10000];
};


#endif	// ARKANOID_STATE_H
