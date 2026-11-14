/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The sound codes the cartridge can produce, and the port staying inside them.
//
//   ./sound_codes_test build/arkanoid.rom
//
// The domain is not a matter of taste. Every writer of a sound code in the
// cartridge is enumerated in SoundCodes.h, and codes 10 to 27 are written by
// nothing: they sit in the table and no instruction selects them. This test
// checks the domain itself, then checks the port's two ways of producing a
// code - the queue and PLAY_SOUND - against it, and against the cartridge,
// which is where each queued code's sequence pointer has to land.
#include <stdio.h>
#include <stdlib.h>

#include "game/Sound.h"
#include "game/SoundCodes.h"
#include "game/SoundPlayer.h"
#include "game/State.h"


namespace {

using namespace SoundCodes;

int gChecks = 0;
int gFailures = 0;


void
Check(bool condition, const char* what, double got, double expected)
{
	gChecks++;
	if (condition) {
		printf("  ok    %-52s %6.0f (expected %6.0f)\n", what, got, expected);
		return;
	}
	gFailures++;
	printf("  FAIL  %-52s %6.0f (expected %6.0f)\n", what, got, expected);
}


// The twelve the game queues, and the eight it writes into SOUND_NUMBER.
const uint8_t kQueued[] = {
	kBallBouncesOnVaus, kBrickDestroyed, kHardBrickHit, kGluedBallCatched,
	kVausFiringX3, kVausDestroyed, kDohHit, kDohDefeated,
	kVausIncreasesSize, kVausGoesThroughPortal, kAlienDestroyed, kSoundLife
};

const uint8_t kDirect[] = {
	kSoundNone, kSoundPause, kGameStartMusic, kLevelStartMusic, kGameOver,
	kGameEnding, kDohLevelStarts, kSoundNop
};

}


int
main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	printf("the sound codes the cartridge can produce\n");

	int reachable = 0;
	int queued = 0;
	for (int code = 0; code < 256; code++) {
		if (IsReachable(static_cast<uint8_t>(code)))
			reachable++;
		if (IsQueued(static_cast<uint8_t>(code)))
			queued++;
	}

	Check(reachable == 20, "twenty codes reach the chip", reachable, 20);
	Check(queued == 12, "twelve of them come through the queue", queued, 12);

	int missing = 0;
	for (size_t i = 0; i < sizeof(kQueued); i++) {
		if (!IsReachable(kQueued[i]))
			missing++;
	}
	Check(missing == 0, "every queued code is one of the twenty", missing, 0);

	// The dead entries: the ones the table holds and nothing selects. Codes 24
	// and 25 are the 36 and 126 second pieces.
	int dead = 0;
	for (int code = 10; code <= 27; code++) {
		if (!IsReachable(static_cast<uint8_t>(code)))
			dead++;
	}
	Check(dead == 18, "codes 10 to 27 are written by nothing", dead, 18);
	Check(!IsReachable(24) && !IsReachable(25),
		"codes 24 and 25 among them", 0, 0);

	// The queue: the twelve land where the original puts them, and none is
	// counted as an outsider.
	State state;
	Sound sound(state);

	int wrong = 0;
	for (size_t i = 0; i < sizeof(kQueued); i++) {
		state.Write(GameRam::kLasersFiring, 0);
		state.Write(GameRam::kSoundsCount, 0);
		sound.Add(kQueued[i]);
		if (state.Read(GameRam::kSoundsBuffer) != kQueued[i])
			wrong++;
		if (state.Read(GameRam::kSoundsCount) != 1)
			wrong++;
	}
	Check(wrong == 0, "the twelve codes queue as the original queues them",
		wrong, 0);
	Check(sound.OutsiderCodes() == 0, "and none of them is an outsider",
		sound.OutsiderCodes(), 0);

	// A code the cartridge never queues is still handled the way the original
	// handles it, and is counted while it is done.
	state.Write(GameRam::kSoundsCount, 0);
	sound.Add(24);
	sound.Add(25);
	Check(sound.OutsiderCodes() == 2,
		"codes 24 and 25 are counted as outsiders", sound.OutsiderCodes(), 2);
	Check(sound.LastOutsider() == 25, "and the last one is remembered",
		sound.LastOutsider(), 25);
	Check(state.Read(GameRam::kSoundsBuffer) == 24,
		"the queue still does what the original does with it",
		state.Read(GameRam::kSoundsBuffer), 24);

	// PLAY_SOUND: none of the twenty is an outsider, and each queued code's
	// sequence pointer lands inside the cartridge.
	SoundPlayer player(state);
	int outside = 0;
	for (size_t i = 0; i < sizeof(kQueued); i++)
		outside += IsReachable(kQueued[i]) ? 0 : 1;
	for (size_t i = 0; i < sizeof(kDirect); i++)
		outside += IsReachable(kDirect[i]) ? 0 : 1;

	for (size_t i = 0; i < sizeof(kQueued); i++) {
		state.Write(SoundPlayer::kCanAddSound, 1);
		state.Write(SoundPlayer::kSoundNumber, kQueued[i]);
		player.Play();
	}
	for (size_t i = 0; i < sizeof(kDirect); i++) {
		state.Write(SoundPlayer::kCanAddSound, 1);
		state.Write(SoundPlayer::kSoundNumber, kDirect[i]);
		player.Play();
	}
	Check(outside == 0, "the twenty are all in the domain", outside, 0);
	Check(player.OutsiderCodes() == 0,
		"and PLAY_SOUND counts none of them as an outsider",
		player.OutsiderCodes(), 0);

	int strayed = 0;
	for (size_t i = 0; i < sizeof(kQueued); i++) {
		state.Write(SoundPlayer::kCanAddSound, 1);
		state.Write(SoundPlayer::kSoundNumber, kQueued[i]);
		player.Play();

		const uint16_t pointer = static_cast<uint16_t>(
			state.Read(SoundPlayer::kStream1 + SoundPlayer::kSequenceLow)
			| (state.Read(SoundPlayer::kStream1 + SoundPlayer::kSequenceHigh)
				<< 8));
		if (pointer < 0x4000 || pointer >= 0xc000) {
			printf("  code %d points its sequence at %04x\n", kQueued[i],
				pointer);
			strayed++;
		}
	}
	Check(strayed == 0, "every queued code points its sequence into the ROM",
		strayed, 0);

	// The fourteen dead codes are counted where they would be played.
	const int before = player.OutsiderCodes();
	for (int code = 14; code <= 27; code++) {
		state.Write(SoundPlayer::kCanAddSound, 1);
		state.Write(SoundPlayer::kSoundNumber, static_cast<uint8_t>(code));
		player.Play();
	}
	Check(player.OutsiderCodes() - before == 14,
		"the fourteen dead codes are counted at PLAY_SOUND",
		player.OutsiderCodes() - before, 14);
	Check(player.LastOutsider() == 27, "and the last one is remembered",
		player.LastOutsider(), 27);

	// The codes the port's own call sites use, against the domain. These are
	// the constants the game queues with; if one of them drifted, the port
	// would be asking the chip for a sound the cartridge never made.
	Check(static_cast<uint8_t>(GameRam::kSoundBallOnVaus) == static_cast<uint8_t>(kBallBouncesOnVaus),
		"the ball's bounce is the cartridge's code 1",
		GameRam::kSoundBallOnVaus, kBallBouncesOnVaus);
	Check(static_cast<uint8_t>(GameRam::kSoundGluedBall) == static_cast<uint8_t>(kGluedBallCatched),
		"the glue catch is code 4", GameRam::kSoundGluedBall,
		kGluedBallCatched);
	Check(static_cast<uint8_t>(GameRam::kSoundVausDestroyed) == static_cast<uint8_t>(kVausDestroyed),
		"the Vaus breaking is code 7", GameRam::kSoundVausDestroyed,
		kVausDestroyed);
	Check(static_cast<uint8_t>(GameRam::kSoundVausThroughPortal) == static_cast<uint8_t>(kVausGoesThroughPortal),
		"the portal is code 193", GameRam::kSoundVausThroughPortal,
		kVausGoesThroughPortal);
	Check(static_cast<uint8_t>(GameRam::kSoundDohHit) == static_cast<uint8_t>(kDohHit), "Doh hit is code 8",
		GameRam::kSoundDohHit, kDohHit);
	Check(static_cast<uint8_t>(GameRam::kSoundDohDefeated) == static_cast<uint8_t>(kDohDefeated),
		"Doh defeated is code 9", GameRam::kSoundDohDefeated, kDohDefeated);
	Check(static_cast<uint8_t>(GameRam::kSoundAlienDestroyed) == static_cast<uint8_t>(kAlienDestroyed),
		"an alien destroyed is code 194", GameRam::kSoundAlienDestroyed,
		kAlienDestroyed);

	printf("%d of %d checks pass\n", gChecks - gFailures, gChecks);
	return gFailures == 0 ? 0 : 1;
}
