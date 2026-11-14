/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The queue drain, and a session of the game with it running.
//
//   ./game_session_test build/arkanoid.rom
//
// Two halves. First the drain itself, GO_ON_AFTER_TRANSITION at 0x41DA: the
// original is stepped through it from the same memory the port is handed, and
// the game variables have to agree afterwards, for a queue that is empty, one
// deep, or full, and with the player in whatever state it is in.
//
// Then the game is run for two thousand frames while its own brick-effect code
// queues sounds through the real ADD_SOUND path. Every code that reaches the
// player has to be one the cartridge could have produced, and the queue has to
// stay the depth the original keeps it at.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/BrickEffects.h"
#include "game/Game.h"
#include "game/SoundCodes.h"
#include "game/SoundPlayer.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kDrain		= 0x41da;	// GO_ON_AFTER_TRANSITION
const uint16_t kRestOfFrame	= 0x40d7;	// where the drain goes on
const uint16_t kComparedFrom = 0xE000;
const uint16_t kComparedTo	= 0xE600;

int gChecks = 0;
int gFailures = 0;


void
Check(bool condition, const char* what, double got, double expected)
{
	gChecks++;
	if (condition) {
		printf("  ok    %-52s %8.0f (expected %8.0f)\n", what, got, expected);
		return;
	}
	gFailures++;
	printf("  FAIL  %-52s %8.0f (expected %8.0f)\n", what, got, expected);
}


bool
ReadFile(const std::string& path, std::vector<uint8_t>& data)
{
	FILE* file = fopen(path.c_str(), "rb");
	if (file == NULL)
		return false;
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (size <= 0) {
		fclose(file);
		return false;
	}
	data.resize(static_cast<size_t>(size));
	size_t read = fread(data.data(), 1, data.size(), file);
	fclose(file);
	return read == data.size();
}


// The original's drain: enter at 0x41DA, leave when it reaches the rest of the
// frame at 0x40D7. It ends with a jump, not a return, so arriving at that
// address is the end of the routine.
bool
RunOriginalDrain(Machine1& machine)
{
	Z80Regs& r = machine.Cpu().r;
	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.af = 0;
	r.bc = 0;
	r.de = 0;
	r.hl = 0;
	r.ix = 0;
	r.iy = 0;
	r.sp = 0xf000;
	r.pc = kDrain;

	for (int step = 0; step < 200000; step++) {
		machine.StepInstruction();
		if (r.pc == kRestOfFrame)
			return true;
	}
	return false;
}


// A cheap generator, so the trials are the same on every run.
struct Generator {
	uint32_t	state = 0x12345678;

	uint32_t
	Next()
	{
		state = state * 1664525 + 1013904223;
		return state >> 16;
	}

	uint8_t
	Range(int from, int to)
	{
		return static_cast<uint8_t>(from + Next() % (to - from + 1));
	}
};


// What a trial can put in a queue slot: a code the game could queue, or a zero,
// which is what an empty slot holds.
uint8_t
SomeCode(Generator& generator)
{
	static const uint8_t kCodes[] = {
		SoundCodes::kBallBouncesOnVaus, SoundCodes::kBrickDestroyed,
		SoundCodes::kHardBrickHit, SoundCodes::kGluedBallCatched,
		SoundCodes::kVausFiringX3, SoundCodes::kVausDestroyed,
		SoundCodes::kDohHit, SoundCodes::kDohDefeated,
		SoundCodes::kVausIncreasesSize, SoundCodes::kVausGoesThroughPortal,
		SoundCodes::kAlienDestroyed, SoundCodes::kSoundLife
	};
	if (generator.Range(0, 4) == 0)
		return SoundCodes::kSoundNone;
	return kCodes[generator.Next() % (sizeof(kCodes) / sizeof(kCodes[0]))];
}

}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: game_session_test <rom>\n");
		return 1;
	}

	std::vector<uint8_t> bios;
	std::vector<uint8_t> logo;
	std::vector<uint8_t> rom;
	if (!ReadFile("third_party/cbios/cbios_main_msx1.rom", bios)
			|| !ReadFile(argv[1], rom)) {
		fprintf(stderr, "cannot read the ROMs\n");
		return 1;
	}
	ReadFile("third_party/cbios/cbios_logo_msx1.rom", logo);

	Machine1 machine;
	if (!machine.LoadBios(bios, logo) || !machine.LoadCartridge(rom)) {
		fprintf(stderr, "bad C-BIOS or ROM size\n");
		return 1;
	}
	machine.Reset();
	for (int frame = 0; frame < 300; frame++)
		machine.RunFrame();

	std::vector<uint8_t> base(0x10000);
	memcpy(base.data(), machine.Ram(), base.size());

	printf("the queue drain, against the original\n");

	Generator generator;
	int mismatched = 0;
	int played = 0;

	for (int trial = 0; trial < 200; trial++) {
		memcpy(machine.Ram(), base.data(), base.size());

		const int depth = generator.Range(0, 8);
		for (int slot = 0; slot < 8; slot++) {
			machine.Ram()[0xe520 + slot]
				= slot < depth ? SomeCode(generator) : 0;
		}
		machine.Ram()[0xe51e] = static_cast<uint8_t>(depth);
		machine.Ram()[0xe5c0] = generator.Range(0, 9);
		machine.Ram()[0xe5c2] = generator.Range(0, 1);	// CAN_ADD_SOUND
		for (int address = 0xe5d3; address < 0xe600; address++)
			machine.Ram()[address] = generator.Range(0, 255);
		for (int reg = 0; reg < 16; reg++)
			machine.GetPsg().Write(reg, generator.Range(0, 255));

		// The port starts from exactly the same memory.
		Game game;
		State& state = game.GetState();
		memcpy(state.Ram(), machine.Ram(), 0x10000);

		const bool hadSound = machine.Ram()[0xe520] != 0;

		if (!RunOriginalDrain(machine)) {
			printf("  trial %d never reached the rest of the frame\n", trial);
			return 1;
		}

		game.DrainSounds();
		const bool drained = game.GetSoundPlayer().Plays() != 0;
		if (drained != hadSound) {
			printf("  trial %d: the port %s a sound the original %s\n", trial,
				drained ? "played" : "skipped",
				hadSound ? "played" : "skipped");
			mismatched++;
		}
		if (drained)
			played++;

		for (uint16_t address = kComparedFrom; address < kComparedTo;
				address++) {
			if (state.Read(address) == machine.Ram()[address])
				continue;
			if (mismatched < 3) {
				printf("  trial %d: %04x original %02x, port %02x\n", trial,
					address, machine.Ram()[address], state.Read(address));
			}
			mismatched++;
			break;
		}
	}

	Check(mismatched == 0,
		"two hundred queue states drain as the original drains them",
		mismatched, 0);
	Check(played > 100, "and more than half had a sound to play", played, 101);

	printf("a session with the drain running\n");

	Game session;
	session.LoadLevel();
	State& state = session.GetState();
	BrickEffects effects(state, session.GetScreen());
	state.Write(GameRam::kTransitionAction, 1);

	KeyMatrix keys = {};
	int queued = 0;
	int tooDeep = 0;
	int strangers = 0;
	int playedInPlay = 0;

	for (int tick = 0; tick < 2000; tick++) {
		// A brick hit through the game's own effect code, which is what puts
		// a code in the queue during play.
		if (tick % 4 == 0) {
			state.Write(GameRam::kBrickRow, 0);
			state.Write(GameRam::kBrickCol,
				static_cast<uint8_t>((tick / 4) % 13));
			state.Write(GameRam::kBricksLeft, 40);
			effects.ActionBrickHit();
			queued++;
			if (state.Read(GameRam::kSoundsCount) > 7)
				tooDeep++;
		}

		session.Tick(keys, 100, 0);
		playedInPlay = session.GetSoundPlayer().Plays();

		if (!SoundCodes::IsReachable(state.Read(GameRam::kSoundNumber)))
			strangers++;
	}

	printf("  %d codes queued, %d played, queue depth now %d\n", queued,
		playedInPlay, state.Read(GameRam::kSoundsCount));

	Check(session.GetSoundPlayer().OutsiderCodes() == 0,
		"nothing outside the domain reached the player",
		session.GetSoundPlayer().OutsiderCodes(), 0);
	Check(strangers == 0, "and nothing outside it ever sat in SOUND_NUMBER",
		strangers, 0);
	Check(playedInPlay > 0, "queued sounds reached the player", playedInPlay, 1);
	Check(tooDeep == 0, "the queue never went past seven", tooDeep, 0);
	Check(state.Read(GameRam::kSoundsCount) <= 1,
		"and it drained as fast as it filled", state.Read(GameRam::kSoundsCount),
		0);

	printf("%d of %d checks pass\n", gChecks - gFailures, gChecks);
	return gFailures == 0 ? 0 : 1;
}
