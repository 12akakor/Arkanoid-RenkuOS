/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of the brick effects.
//
//   ./brickeffect_test build/arkanoid.rom [cases]
//
// First the dispatch: APPLY_BRICK_HIT_EFFECT (0xAA05) is called on the
// reference machine for every brick of every level and the machine is stepped
// until it reaches one of the five actions, so the action the port names is
// compared with the one the original actually jumps to.
//
// Then the actions themselves - action_brick_hit (0xAAEF),
// action_hard_brick_hit (0xAAC7) and action_unbreakable_brick_hit (0xAAA4) -
// in a few hundred random states each, comparing the game's variables and the
// whole 16 KB of video memory, since destroying a brick erases it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/BrickEffects.h"
#include "game/Screen.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kApplyBrickHitEffect = 0xAA05;

const uint16_t kComparedFrom = 0xE000;
const uint16_t kComparedTo = 0xE600;

// The five actions, at the addresses the cartridge's table names.
struct ActionEntry {
	uint16_t	address;
	uint8_t		action;
};

const ActionEntry kActions[] = {
	{ 0xAAEF, BrickEffects::kActionBrickHit },
	{ 0xAAC1, BrickEffects::kActionBrickHitAndCapsule },
	{ 0xAAC7, BrickEffects::kActionHardBrickHit },
	{ 0xAAA4, BrickEffects::kActionUnbreakableBrickHit },
	{ 0xAA9C, BrickEffects::kActionResetUnusedVars }
};

const int kActionCount = sizeof(kActions) / sizeof(kActions[0]);

// The actions the port has, at their own entry addresses.
const uint16_t kActionEntries[] = { 0xAAEF, 0xAAC7, 0xAAA4 };
const char* const kActionNames[] = { "action_brick_hit",
	"action_hard_brick_hit", "action_unbreakable_brick_hit" };


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


uint16_t
Table(int ball)
{
	return GameRam::kBallTables
		+ static_cast<uint16_t>(GameRam::kBallTableLength * ball);
}


uint16_t
Sprite(int sprite)
{
	return GameRam::kBallSprites
		+ static_cast<uint16_t>(GameRam::kSpriteLength * sprite);
}


void
Prepare(Machine1& machine, uint16_t routine)
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
	r.ix = Sprite(0);
	r.iy = Table(0);
	r.sp = 0xF000;
	r.sp -= 2;
	machine.Write(r.sp, static_cast<uint8_t>(kSentinel & 0xff));
	machine.Write(r.sp + 1, static_cast<uint8_t>(kSentinel >> 8));
	r.pc = routine;
}


// Runs the original until it arrives at an action, and says which.
bool
CallDispatch(Machine1& machine, uint8_t& action)
{
	Prepare(machine, kApplyBrickHitEffect);

	for (int step = 0; step < 200000; step++) {
		machine.StepInstruction();
		for (int i = 0; i < kActionCount; i++) {
			if (machine.Cpu().r.pc == kActions[i].address) {
				action = kActions[i].action;
				return true;
			}
		}
		if (machine.Cpu().r.pc == kSentinel)
			return false;
	}
	return false;
}


// Runs one action to its end.
bool
CallAction(Machine1& machine, uint16_t routine)
{
	Prepare(machine, routine);

	for (int step = 0; step < 200000; step++) {
		machine.StepInstruction();
		if (machine.Cpu().r.pc == kSentinel)
			return true;
	}
	return false;
}


// A small deterministic generator, so a failing case can be reproduced.
struct Generator {
	uint32_t	state = 987654321;

	uint32_t
	Next()
	{
		state = state * 1103515245u + 12345u;
		return (state >> 8) & 0xffffff;
	}

	int Range(int low, int high) { return low + Next() % (high - low + 1); }
};

}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: brickeffect_test <rom> [cases]\n");
		return 1;
	}

	std::vector<uint8_t> bios;
	std::vector<uint8_t> logo;
	std::vector<uint8_t> rom;
	if (!ReadFile("third_party/cbios/cbios_main_msx1.rom", bios)
		|| !ReadFile(argv[1], rom)) {
		fprintf(stderr, "cannot read the C-BIOS or the ROM\n");
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

	const int cases = argc > 2 ? atoi(argv[2]) : 300;
	Generator random;
	int compared = 0;
	int failed = 0;

	// The dispatch, over every brick of every level.
	for (int level = 0; level < 32; level++) {
		for (int row = 0; row < 12; row++) {
			for (int column = 0; column < 11; column++) {
				std::vector<uint8_t> initial = base;
				initial[GameRam::kLevel] = static_cast<uint8_t>(level);
				initial[GameRam::kBrickRow] = static_cast<uint8_t>(row);
				initial[GameRam::kBrickCol]
					= static_cast<uint8_t>(column);

				memcpy(machine.Ram(), initial.data(), initial.size());
				uint8_t expectedAction = 0xff;
				if (!CallDispatch(machine, expectedAction)) {
					printf("  APPLY_BRICK_HIT_EFFECT: the original did not"
						" reach an action (level %d, row %d, column %d)\n",
						level, row, column);
					failed++;
					continue;
				}
				std::vector<uint8_t> expected(0x10000);
				memcpy(expected.data(), machine.Ram(), expected.size());

				State state;
				memcpy(state.Ram(), initial.data(), initial.size());
				Screen screen;
				BrickEffects effects(state, screen);
				const uint8_t action = effects.ApplyHitEffect(0);

				compared++;
				if (action != expectedAction) {
					if (failed < 10)
						printf("  level %d row %d column %d: action"
							" original %d, port %d\n", level, row, column,
							expectedAction, action);
					failed++;
					continue;
				}

				for (uint16_t address = kComparedFrom;
						address < kComparedTo; address++) {
					if (expected[address] == state.Ram()[address])
						continue;
					if (failed < 10)
						printf("  level %d row %d column %d: 0x%04x"
							" original %02x, port %02x\n", level, row,
							column, address, expected[address],
							state.Ram()[address]);
					failed++;
					break;
				}
			}
		}
	}

	// The actions, in random states.
	for (int which = 0; which < 3; which++) {
		for (int index = 0; index < cases; index++) {
			std::vector<uint8_t> initial = base;

			const int row = random.Range(0, 11);
			const int column = random.Range(0, 10);
			initial[GameRam::kBrickRow] = static_cast<uint8_t>(row);
			initial[GameRam::kBrickCol] = static_cast<uint8_t>(column);
			initial[GameRam::kBricksLeft]
				= static_cast<uint8_t>(random.Range(0, 4) == 0
					? 1 : random.Range(1, 255));
			initial[GameRam::kGameState]
				= static_cast<uint8_t>(random.Range(0, 3));
			initial[GameRam::kLives]
				= static_cast<uint8_t>(random.Range(0, 6));
			initial[GameRam::kActionSkewnessCounter]
				= static_cast<uint8_t>(random.Range(0, 2) == 0
					? 19 : random.Range(0, 19));
			initial[GameRam::kLasersFiring] = 0;
			initial[GameRam::kSoundsCount]
				= static_cast<uint8_t>(random.Range(0, 7));

			for (int i = 0; i < 3; i++) {
				initial[GameRam::kScoreBcd + i]
					= static_cast<uint8_t>(random.Range(0, 0x99));
				initial[GameRam::kScoreLifeBcd + i]
					= static_cast<uint8_t>(random.Range(0, 0x99));
				initial[GameRam::kHighScoreBcd + i]
					= static_cast<uint8_t>(random.Range(0, 0x99));
			}
			initial[GameRam::kScoreLifeBcd + 3]
				= static_cast<uint8_t>(random.Range(0, 1));

			// The hard brick hit counters and the tilemap are what the two
			// brick kinds and the points read.
			for (int i = 0; i < 12 * 11; i++) {
				initial[GameRam::kHardBricksRemainingHits + i]
					= static_cast<uint8_t>(random.Range(0, 3));
			}
			for (int i = 0; i < 22 * 12; i++) {
				initial[GameRam::kBackgroundTilemap + i]
					= static_cast<uint8_t>(random.Range(0, 3) == 0
						? 0x23 + 2 * random.Range(0, 4)
						: random.Next());
			}

			uint8_t* vram = machine.Vdp().VramWritable();
			for (int i = 0; i < 0x4000; i++)
				vram[i] = static_cast<uint8_t>(random.Next());

			memcpy(machine.Ram(), initial.data(), initial.size());
			if (!CallAction(machine, kActionEntries[which])) {
				printf("  %s: the original did not return (case %d)\n",
					kActionNames[which], index);
				failed++;
				continue;
			}
			std::vector<uint8_t> expected(0x10000);
			memcpy(expected.data(), machine.Ram(), expected.size());
			std::vector<uint8_t> expectedVram(0x4000);
			memcpy(expectedVram.data(), machine.Vdp().Vram(),
				expectedVram.size());

			State state;
			memcpy(state.Ram(), initial.data(), initial.size());
			Screen screen;
			for (int reg = 0; reg < Screen::kRegisters; reg++)
				screen.SetRegister(reg, machine.Vdp().Register(reg));
			memcpy(screen.VramPointer(), expectedVram.data(),
				Screen::kVramSize);

			BrickEffects effects(state, screen);
			switch (which) {
				case 0:
					effects.ActionBrickHit();
					break;
				case 1:
					effects.ActionHardBrickHit();
					break;
				default:
					effects.ActionUnbreakableBrickHit();
					break;
			}

			compared++;
			bool differs = false;
			for (uint16_t address = kComparedFrom; address < kComparedTo;
					address++) {
				if (expected[address] == state.Ram()[address])
					continue;
				if (failed < 10)
					printf("  %s case %d: 0x%04x original %02x, port %02x"
						" (row %d column %d, bricks left %d, state %d)\n",
						kActionNames[which], index, address,
						expected[address], state.Ram()[address], row,
						column, initial[GameRam::kBricksLeft],
						initial[GameRam::kGameState]);
				failed++;
				differs = true;
				break;
			}
			if (differs)
				continue;

			for (uint16_t address = 0; address < Screen::kVramSize;
					address++) {
				if (expectedVram[address] == screen.Vram(address))
					continue;
				if (failed < 10)
					printf("  %s case %d: vram 0x%04x original %02x, port"
						" %02x (row %d column %d)\n", kActionNames[which],
						index, address, expectedVram[address],
						screen.Vram(address), row, column);
				failed++;
				break;
			}
		}
	}

	printf("%d of %d brick effect computations agree with the original\n",
		compared - failed, compared);
	return failed == 0 ? 0 : 1;
}
