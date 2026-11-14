/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The sound codes this cartridge can produce, and only those.
//
// The twenty-eight entries of TBL_SOUND_PARAMS are not twenty-eight sounds the
// game uses. Every writer of a sound code in the cartridge is one of these:
//
//   the twelve it queues through ADD_SOUND (0x5BEF), from twenty call sites,
//   every one of which loads its argument from a constant:
//
//     1    SOUND_BALL_BOUNCES_ON_VAUS      0x9939 0x9BE3
//     2    SOUND_BRICK_DESTROYED           0xAB00
//     3    SOUND_HARD_BRICK_HIT            0xAB31
//     4    SOUND_GLUED_BALL_CATCHED        0x9C00
//     6    SOUND_VAUS_FIRING_X3            0x709B
//     7    SOUND_VAUS_DESTROYED            0x7AAC 0x9B55
//     8    SOUND_DOH_HIT                   0x96E4
//     9    SOUND_DOH_DEFEATED              0x9704
//     192  SOUND_VAUS_INCREASES_SIZE_H     0xB235
//     193  SOUND_VAUS_GOES_THROUGH_PORTAL_H 0x699B 0x6A07
//     194  SOUND_ALIEN_DESTROYED           0x7926 0x7988 0x79BD 0x79DA 0x79F7
//     197  SOUND_LIFE                     0x533D 0xB2A1
//
//   the eight it writes into SOUND_NUMBER as constants:
//
//     0    SOUND_NOP_0                    0x4ECD  clears the number
//     5    SOUND_PAUSE                    0x40E6
//     195  SOUND_GAME_START_MUSIC         0x4C84
//     196  SOUND_LEVEL_START_MUSIC        0x4E93
//     198  SOUND_GAME_OVER                0x7C26
//     199  SOUND_GAME_ENDING              0x7BCF
//     200  SOUND_DOH_LEVEL_STARTS         0x4EA5
//     248  SOUND_NOP                      0x40C3
//
//   and the one write at 0x41E2 that copies the front of the queue into
//   SOUND_NUMBER, so the codes above are what it can carry.
//
// Codes 10 to 13 and 14 to 27 are never written by anything: they exist in the
// table and nothing selects them. Playing one would be playing a sound the
// original never made.


#ifndef ARKANOID_SOUNDCODES_H
#define ARKANOID_SOUNDCODES_H


#include <stdint.h>


namespace SoundCodes {


enum Code : uint8_t {
	kSoundNone				= 0,
	kBallBouncesOnVaus		= 1,
	kBrickDestroyed			= 2,
	kHardBrickHit			= 3,
	kGluedBallCatched		= 4,
	kSoundPause				= 5,
	kVausFiringX3			= 6,
	kVausDestroyed			= 7,
	kDohHit					= 8,
	kDohDefeated			= 9,
	kVausIncreasesSize		= 192,
	kVausGoesThroughPortal	= 193,
	kAlienDestroyed			= 194,
	kGameStartMusic			= 195,
	kLevelStartMusic		= 196,
	kSoundLife				= 197,
	kGameOver				= 198,
	kGameEnding				= 199,
	kDohLevelStarts			= 200,
	kSoundNop				= 248
};


// The twelve the game queues. The eight above that are not here are written
// into SOUND_NUMBER directly, and none of them is ever queued.
inline bool
IsQueued(uint8_t code)
{
	switch (code) {
		case kBallBouncesOnVaus:
		case kBrickDestroyed:
		case kHardBrickHit:
		case kGluedBallCatched:
		case kVausFiringX3:
		case kVausDestroyed:
		case kDohHit:
		case kDohDefeated:
		case kVausIncreasesSize:
		case kVausGoesThroughPortal:
		case kAlienDestroyed:
		case kSoundLife:
			return true;
	}
	return false;
}


// Every code the cartridge can put in SOUND_NUMBER, queued or direct.
inline bool
IsReachable(uint8_t code)
{
	if (IsQueued(code))
		return true;

	switch (code) {
		case kSoundNone:
		case kSoundPause:
		case kGameStartMusic:
		case kLevelStartMusic:
		case kGameOver:
		case kGameEnding:
		case kDohLevelStarts:
		case kSoundNop:
			return true;
	}
	return false;
}


} // namespace SoundCodes


#endif	// ARKANOID_SOUNDCODES_H
