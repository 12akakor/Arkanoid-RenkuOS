/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of PLAY_SOUND.
//
//   ./soundplayer_test build/arkanoid.rom [cases]
//
// The routine is called on the reference machine with a random sound code, a
// random pair of stream states and the sound queue either open or inhibited,
// and run on the same memory by the port. The game's variables have to agree;
// nothing touches video memory.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/SoundPlayer.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kPlaySound = 0xB400;	// jp PLAY_SOUND

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
	r.pc = kPlaySound;

	for (int step = 0; step < 500000; step++) {
		machine.StepInstruction();
		if (r.pc == kSentinel)
			return true;
	}
	return false;
}


struct Generator {
	uint32_t	state = 1234321;

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
		fprintf(stderr, "usage: soundplayer_test <rom> [cases]\n");
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

	for (int index = 0; index < cases; index++) {
		std::vector<uint8_t> initial = base;

		// Every code the game can ask for, including the pieces above 192 and
		// the requests above 240 that go straight at the chip.
		initial[GameRam::kSoundNumber]
			= static_cast<uint8_t>(generator.Range(0, 255));
		initial[GameRam::kCanAddSound]
			= static_cast<uint8_t>(generator.Range(0, 1));

		// Both streams, in any state, including a priority that must not be
		// interrupted.
		for (int stream = 0; stream < 2; stream++) {
			const uint16_t address = stream == 0
				? SoundPlayer::kStream1 : SoundPlayer::kStream2;
			for (int i = 0; i < 10; i++) {
				initial[address + i] = static_cast<uint8_t>(
					generator.Range(0, 255));
			}
		}

		memcpy(machine.Ram(), initial.data(), initial.size());
		// The chip's registers as they stand, which the player reads back.
		uint8_t startRegs[16];
		for (int reg = 0; reg < 16; reg++)
			startRegs[reg] = machine.GetPsg().Register(reg);
		if (!CallOriginal(machine)) {
			printf("  case %d: the original did not return\n", index);
			failed++;
			continue;
		}
		std::vector<uint8_t> expected(0x10000);
		memcpy(expected.data(), machine.Ram(), expected.size());
		uint8_t expectedRegs[16];
		for (int reg = 0; reg < 16; reg++)
			expectedRegs[reg] = machine.GetPsg().Register(reg);

		State state;
		memcpy(state.Ram(), initial.data(), initial.size());
		SoundPlayer player(state);
		for (int reg = 0; reg < 16; reg++)
			player.SetPsg(reg, startRegs[reg]);
		player.Play();

		compared++;

		bool differs = false;
		for (uint16_t address = kComparedFrom; address < kComparedTo;
				address++) {
			if (expected[address] == state.Ram()[address])
				continue;
			if (failed < 10)
				printf("  case %d: 0x%04x original %02x, port %02x"
					" (code %d, canAdd %d)\n", index, address,
					expected[address], state.Ram()[address],
					initial[GameRam::kSoundNumber],
					initial[GameRam::kCanAddSound]);
			failed++;
			differs = true;
			break;
		}
		if (differs)
			continue;

		for (int reg = 0; reg < 16; reg++) {
			const uint8_t got = player.ReadPsg(reg);
			if (got == expectedRegs[reg])
				continue;
			if (failed < 10)
				printf("  case %d: psg %d original %02x, port %02x"
					" (code %d)\n", index, reg, expectedRegs[reg], got,
					initial[GameRam::kSoundNumber]);
			failed++;
			break;
		}
	}

	printf("%d of %d sound queues agree with the original\n", compared - failed,
		compared);
	return failed == 0 ? 0 : 1;
}
