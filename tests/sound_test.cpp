/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of the sound queue and the brick action that only clears
// two variables.
//
//   ./sound_test build/arkanoid.rom [cases]
//
// ADD_SOUND (0x5BEF) is called on the reference machine with a sound code and
// a queue state, and run on the same memory by the port; the whole game
// variable area has to agree. action_reset_unused_vars (0xAA9C) goes the same
// way.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/BrickEffects.h"
#include "game/Sound.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kAddSound = 0x5BEF;
const uint16_t kResetUnusedVars = 0xAA9C;

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
CallOriginal(Machine1& machine, uint16_t routine, uint8_t argument,
	bool& carry)
{
	Z80Regs& r = machine.Cpu().r;
	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.a = argument;
	r.f = 0;
	r.bc = 0;
	r.de = 0;
	r.hl = 0;
	r.ix = 0;
	r.iy = 0;
	r.sp = 0xF000;
	r.sp -= 2;
	machine.Write(r.sp, static_cast<uint8_t>(kSentinel & 0xff));
	machine.Write(r.sp + 1, static_cast<uint8_t>(kSentinel >> 8));
	r.pc = routine;

	for (int step = 0; step < 200000; step++) {
		machine.StepInstruction();
		if (r.pc == kSentinel) {
			carry = (r.f & Z80_CF) != 0;
			return true;
		}
	}
	return false;
}


// A small deterministic generator, so a failing case can be reproduced.
struct Generator {
	uint32_t	state = 135792468;

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
		fprintf(stderr, "usage: sound_test <rom> [cases]\n");
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

	const int cases = argc > 2 ? atoi(argv[2]) : 400;
	Generator random;
	int compared = 0;
	int failed = 0;

	for (int which = 0; which < 2; which++) {
		for (int index = 0; index < cases; index++) {
			std::vector<uint8_t> initial = base;

			const int sound = random.Range(0, 255);
			if (which == 0) {
				// Every queue state, including an empty one and the
				// eighth slot the original never uses.
				initial[GameRam::kSoundsCount]
					= static_cast<uint8_t>(random.Range(0, 8));
				initial[GameRam::kLasersFiring]
					= random.Range(0, 3) == 0
						? static_cast<uint8_t>(random.Range(1, 255)) : 0;
				for (int i = 0; i < 8; i++) {
					initial[GameRam::kSoundsBuffer + i]
						= static_cast<uint8_t>(random.Next());
				}
			} else {
				initial[GameRam::kBrickUnused1]
					= static_cast<uint8_t>(random.Next());
				initial[GameRam::kBrickUnused2]
					= static_cast<uint8_t>(random.Next());
			}

			memcpy(machine.Ram(), initial.data(), initial.size());
			bool carry = false;
			if (!CallOriginal(machine, which == 0 ? kAddSound
					: kResetUnusedVars, static_cast<uint8_t>(sound),
					carry)) {
				printf("  routine %d: the original did not return (case"
					" %d)\n", which, index);
				failed++;
				continue;
			}
			std::vector<uint8_t> expected(0x10000);
			memcpy(expected.data(), machine.Ram(), expected.size());

			State state;
			memcpy(state.Ram(), initial.data(), initial.size());
			if (which == 0) {
				Sound queue(state);
				queue.Add(static_cast<uint8_t>(sound));
			} else {
				Screen screen;
				BrickEffects effects(state, screen);
				effects.ResetUnusedVars();
			}

			compared++;
			for (uint16_t address = kComparedFrom; address < kComparedTo;
					address++) {
				if (expected[address] == state.Ram()[address])
					continue;
				if (failed < 10)
					printf("  routine %d case %d: 0x%04x original %02x,"
						" port %02x (sound %d, count %d, lasers %d)\n",
						which, index, address, expected[address],
						state.Ram()[address], sound,
						initial[GameRam::kSoundsCount],
						initial[GameRam::kLasersFiring]);
				failed++;
				break;
			}
		}
	}

	printf("%d of %d sound queue and variable resets agree with the"
		" original\n", compared - failed, compared);
	return failed == 0 ? 0 : 1;
}
