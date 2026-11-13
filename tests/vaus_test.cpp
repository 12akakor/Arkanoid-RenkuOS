/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of EXECUTE_VAUS_ACTION.
//
//   ./vaus_test build/arkanoid.rom [cases]
//
// The routine is called on the reference machine with the two states that are
// ported - waiting, which becomes KEEP, and KEEP itself - and run on the same
// memory by the port. The game's variables, the four Vaus sprites and the video
// memory have to agree.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/Screen.h"
#include "game/State.h"
#include "game/Vaus.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kExecuteVausAction = 0x68C4;

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


bool
CallOriginal(Machine1& machine, bool& carry)
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
	r.ix = GameRam::kVausTable;
	r.iy = GameRam::kSpriteParamsBase;
	r.sp = 0xF000;
	r.sp -= 2;
	machine.Write(r.sp, static_cast<uint8_t>(kSentinel & 0xff));
	machine.Write(r.sp + 1, static_cast<uint8_t>(kSentinel >> 8));
	r.pc = kExecuteVausAction;

	for (int step = 0; step < 200000; step++) {
		machine.StepInstruction();
		if (r.pc == kSentinel) {
			carry = (r.f & Z80_CF) != 0;
			return true;
		}
	}
	return false;
}


struct Generator {
	uint32_t	state = 424242;

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
		fprintf(stderr, "usage: vaus_test <rom> [cases]\n");
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
	Generator generator;
	int compared = 0;
	int failed = 0;

	for (int index = 0; index < cases; index++) {
		std::vector<uint8_t> initial = base;

		initial[GameRam::kVausTable + GameRam::kVausIdxActionState]
			= static_cast<uint8_t>(generator.Range(0, 1));
		initial[GameRam::kVausTable + GameRam::kVausIdxResizing] = 0;
		initial[GameRam::kVausTable + GameRam::kVausIdxHasLaser]
			= static_cast<uint8_t>(generator.Range(0, 1));
		initial[GameRam::kResetVausSpritePosition]
			= static_cast<uint8_t>(generator.Range(0, 1));
		initial[GameRam::kGameState]
			= static_cast<uint8_t>(generator.Range(0, 3));
		initial[GameRam::kUseVausPaddle]
			= static_cast<uint8_t>(generator.Range(0, 1));
		initial[GameRam::kControls]
			= static_cast<uint8_t>(generator.Range(0, 0x0f));
		initial[GameRam::kBallXDemo]
			= static_cast<uint8_t>(generator.Range(0, 255));
		// The paddle count is sixteen bits; every value the scaling can see.
		const uint16_t count = static_cast<uint16_t>(generator.Range(0, 400));
		initial[GameRam::kPaddleCount] = static_cast<uint8_t>(count);
		initial[GameRam::kPaddleCount + 1] = static_cast<uint8_t>(count >> 8);
		initial[GameRam::kPortalOpen] = 0;

		for (int i = 0; i < 4 * GameRam::kSpriteParamsLength; i++) {
			initial[GameRam::kSpriteParamsBase + i]
				= static_cast<uint8_t>(generator.Range(0, 255));
		}

		uint8_t* vram = machine.Vdp().VramWritable();
		for (int i = 0; i < 0x4000; i++)
			vram[i] = static_cast<uint8_t>(generator.Next());

		memcpy(machine.Ram(), initial.data(), initial.size());
		bool carry = false;
		if (!CallOriginal(machine, carry)) {
			printf("  case %d: the original did not return\n", index);
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
		memcpy(screen.VramPointer(), expectedVram.data(), Screen::kVramSize);
		Vaus vaus(state, screen);
		vaus.ExecuteAction();

		compared++;

		bool differs = false;
		for (uint16_t address = kComparedFrom; address < kComparedTo;
				address++) {
			if (expected[address] == state.Ram()[address])
				continue;
			if (failed < 10)
				printf("  case %d: 0x%04x original %02x, port %02x"
					" (state %d, game %d, controls %d, paddle %d)\n", index,
					address, expected[address], state.Ram()[address],
					initial[GameRam::kVausTable], initial[GameRam::kGameState],
					initial[GameRam::kControls], count);
			failed++;
			differs = true;
			break;
		}
		if (differs)
			continue;

		for (uint16_t address = 0; address < Screen::kVramSize; address++) {
			if (expectedVram[address] == screen.Vram(address))
				continue;
			if (failed < 10)
				printf("  case %d: vram 0x%04x original %02x, port %02x\n",
					index, address, expectedVram[address],
					screen.Vram(address));
			failed++;
			break;
		}
	}

	printf("%d of %d Vaus actions agree with the original\n", compared - failed,
		compared);
	return failed == 0 ? 0 : 1;
}
