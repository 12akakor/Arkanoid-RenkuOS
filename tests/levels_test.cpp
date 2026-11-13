/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of the level loader's three routines.
//
//   ./levels_test build/arkanoid.rom [cases]
//
// ADD_BRICKS_TO_TILEMAP (0x5C15), ADD_BACKGROUND_TO_TILEMAP (0x5D9A) and
// DRAW_BACKGROUND_TILEMAP (0x5D77), each called on the reference machine with a
// random level and transition, and run on the same memory by the port. The
// game's variables and the video memory have to agree.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/Levels.h"
#include "game/Screen.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kBricks = 0x5C15;
const uint16_t kBackground = 0x5D9D;
const uint16_t kDraw = 0x5D79;
const uint16_t kColorize = 0x5BBD;
const uint16_t kRoundMessage = 0x7204;

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
CallOriginal(Machine1& machine, uint16_t routine)
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
	r.pc = routine;

	for (int step = 0; step < 500000; step++) {
		machine.StepInstruction();
		if (r.pc == kSentinel)
			return true;
	}
	return false;
}


struct Generator {
	uint32_t	state = 909090909;

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
		fprintf(stderr, "usage: levels_test <rom> [cases]\n");
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
	Generator generator;
	int compared = 0;
	int failed = 0;

	const uint16_t routines[5] = { kBricks, kBackground, kDraw, kColorize,
		kRoundMessage };
	const char* names[5] = { "ADD_BRICKS_TO_TILEMAP",
		"ADD_BACKGROUND_TO_TILEMAP", "DRAW_BACKGROUND_TILEMAP",
		"COLORIZE_BACKGROUND", "WRITE_ROUND_MSG" };

	for (int which = 0; which < 5; which++) {
		for (int index = 0; index < cases; index++) {
			std::vector<uint8_t> initial = base;

			// Every level the pointer table has, and every transition.
			initial[GameRam::kLevel]
				= static_cast<uint8_t>(generator.Range(0, 31));
			initial[GameRam::kLevelTransitionType]
				= static_cast<uint8_t>(generator.Range(0, 2));
			// The displayed level, as packed decimal.
			initial[GameRam::kLevelDispBcd] = static_cast<uint8_t>(
				16 * generator.Range(0, 9) + generator.Range(0, 9));
			initial[GameRam::kBricksLeft]
				= static_cast<uint8_t>(generator.Range(0, 0x40));

			// A map to replay, for the same-level case.
			for (int i = 0; i < 17; i++)
				initial[0xE027 + i] = static_cast<uint8_t>(generator.Next());

			// The tilemap the bricks are drawn over.
			for (int i = 0; i < 12 * 32; i++) {
				initial[GameRam::kBackgroundTilemap + i]
					= static_cast<uint8_t>(generator.Range(0, 0x60));
			}

			uint8_t* vram = machine.Vdp().VramWritable();
			for (int i = 0; i < 0x4000; i++)
				vram[i] = static_cast<uint8_t>(generator.Next());

			memcpy(machine.Ram(), initial.data(), initial.size());
			if (!CallOriginal(machine, routines[which])) {
				printf("  %s case %d: the original did not return\n",
					names[which], index);
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

			Levels levels(state, screen);
			switch (which) {
				case 0:
					levels.AddBricksToTilemap();
					break;
				case 1:
					levels.AddBackgroundToTilemap();
					break;
				case 2:
					levels.DrawBackgroundTilemap();
					break;
				case 3:
					levels.ColorizeBackground();
					break;
				default:
					levels.WriteRoundMessage();
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
						" (level %d, transition %d)\n", names[which], index,
						address, expected[address], state.Ram()[address],
						initial[GameRam::kLevel],
						initial[GameRam::kLevelTransitionType]);
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
					printf("  %s case %d: vram 0x%04x original %02x,"
						" port %02x\n", names[which], index, address,
						expectedVram[address], screen.Vram(address));
				failed++;
				break;
			}
		}
	}

	printf("%d of %d level loader calls agree with the original\n",
		compared - failed, compared);
	return failed == 0 ? 0 : 1;
}
