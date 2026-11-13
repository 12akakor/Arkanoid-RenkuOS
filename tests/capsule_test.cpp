/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of the capsule spawning.
//
//   ./capsule_test build/arkanoid.rom [cases]
//
// SET_PROPER_RANDOM_CAPSULE_TYPE (0xB028) is run on the reference machine and by
// the port in five situations:
//
//   - fewer than four bricks left, and a capsule already falling: neither does
//     anything;
//   - the capsule counter says "no capsule this time": type 5, compared exactly;
//   - the lives counter says "a life": type 6, compared exactly;
//   - the portal is open, so the capsule counter does not run at all;
//   - the capsule type is drawn from one of the four tables. The original takes
//     the entry from the Z80's refresh register, which is not game state and
//     differs between runs, so this case asks whether the port can produce the
//     original's outcome for one of the eight entries of the table - which is
//     all the information the original had.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/Capsules.h"
#include "game/Random.h"
#include "game/Screen.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kSpawnCapsule = 0xB028;

const uint16_t kComparedFrom = 0xE000;
const uint16_t kComparedTo = 0xE600;


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


bool
CallOriginal(Machine1& machine)
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
	r.sp = 0xF000;
	r.sp -= 2;
	machine.Write(r.sp, static_cast<uint8_t>(kSentinel & 0xff));
	machine.Write(r.sp + 1, static_cast<uint8_t>(kSentinel >> 8));
	r.pc = kSpawnCapsule;

	for (int step = 0; step < 200000; step++) {
		machine.StepInstruction();
		if (r.pc == kSentinel)
			return true;
	}
	return false;
}


// A small deterministic generator, so a failing case can be reproduced.
struct Generator {
	uint32_t	state = 112233445;

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
		fprintf(stderr, "usage: capsule_test <rom> [cases]\n");
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

	const int cases = argc > 2 ? atoi(argv[2]) : 200;
	Generator generator;
	int compared = 0;
	int failed = 0;

	enum { kScenarioCount = 5 };
	static const char* names[] = { "no capsule under four bricks",
		"capsule already falling", "capsule counter says none",
		"lives counter says a life", "portal open" };

	for (int scenario = 0; scenario < kScenarioCount; scenario++) {
		for (int index = 0; index < cases; index++) {
			std::vector<uint8_t> initial = base;

			initial[GameRam::kBrickRow]
				= static_cast<uint8_t>(generator.Range(0, 11));
			initial[GameRam::kBrickCol]
				= static_cast<uint8_t>(generator.Range(0, 10));
			initial[GameRam::kCapsuleIsFalling] = 0;
			initial[GameRam::kPortalOpen] = 0;
			initial[GameRam::kLifeObtainedFlag] =
				static_cast<uint8_t>(generator.Range(0, 1));
			initial[GameRam::kGluingStatus] =
				static_cast<uint8_t>(generator.Range(0, 1));
			initial[GameRam::kVausIsEnlarged] =
				static_cast<uint8_t>(generator.Range(0, 1));
			initial[GameRam::kVausHasLasers] =
				static_cast<uint8_t>(generator.Range(0, 1));

			// The counters, set so that the scenario takes the path it is
			// meant to take.
			initial[GameRam::kCapsulesLeft] = 33;
			initial[GameRam::kCapsulesRandomNum] = 5;
			initial[GameRam::kCapsuleLivesCounterL] = 0x32;
			initial[GameRam::kCapsuleLivesCounterH] = 5;
			initial[GameRam::kBricksLeft] = 20;

			switch (scenario) {
				case 0:
					initial[GameRam::kBricksLeft]
						= static_cast<uint8_t>(generator.Range(0, 3));
					break;
				case 1:
					initial[GameRam::kCapsuleIsFalling]
						= static_cast<uint8_t>(generator.Range(1, 255));
					break;
				case 2:
					initial[GameRam::kCapsulesLeft] = 1;
					initial[GameRam::kCapsulesRandomNum] = 0;
					initial[GameRam::kLifeObtainedFlag] = 1;
					break;
				case 3:
					initial[GameRam::kCapsuleLivesCounterL] = 1;
					initial[GameRam::kCapsuleLivesCounterH] = 0;
					initial[GameRam::kLifeObtainedFlag] = 0;
					break;
				default:
					initial[GameRam::kPortalOpen] = 1;
					initial[GameRam::kCapsuleLivesCounterL] = 2;
					initial[GameRam::kCapsuleLivesCounterH] = 0;
					initial[GameRam::kLifeObtainedFlag] = 0;
					break;
			}

			memcpy(machine.Ram(), initial.data(), initial.size());
			if (!CallOriginal(machine)) {
				printf("  %s: the original did not return (case %d)\n",
					names[scenario], index);
				failed++;
				continue;
			}
			std::vector<uint8_t> expected(0x10000);
			memcpy(expected.data(), machine.Ram(), expected.size());

			// The port, with the eight possible entries of the table.
			bool matched = false;
			for (int entry = 0; entry < 8; entry++) {
				State state;
				memcpy(state.Ram(), initial.data(), initial.size());
				Screen screen;
				Random random;
				random.SetFixed(static_cast<uint8_t>(entry));
				Capsules capsules(state, screen, random);
				capsules.SpawnFromBrick();

				bool equal = true;
				for (uint16_t address = kComparedFrom;
						address < kComparedTo; address++) {
					if (expected[address] != state.Ram()[address]) {
						equal = false;
						if (scenario < 4 && failed < 10) {
							printf("  %s case %d: 0x%04x original %02x,"
								" port %02x\n", names[scenario], index,
								address, expected[address],
								state.Ram()[address]);
						}
						break;
					}
				}
				if (equal) {
					matched = true;
					break;
				}
			}

			compared++;
			if (!matched) {
				if (scenario < 4)
					failed++;
				else if (failed < 10) {
					printf("  %s case %d: no table entry reproduces the"
						" original (row %d column %d)\n", names[scenario],
						index, initial[GameRam::kBrickRow],
						initial[GameRam::kBrickCol]);
					failed++;
				} else
					failed++;
			}
		}
	}

	printf("%d of %d capsule spawnings agree with the original\n",
		compared - failed, compared);
	return failed == 0 ? 0 : 1;
}
