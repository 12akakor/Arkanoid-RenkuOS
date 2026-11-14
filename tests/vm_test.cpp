/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of the whole sound interpreter.
//
//   ./vm_test build/arkanoid.rom [ticks] [codes]
//
// For each sound code: play it on the reference machine, then run the tick -
// registers, both streams, the command dispatcher and the three updaters - and
// compare the chip and the game's variables after every single tick. This is
// the test the command handlers exist for.
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
const uint16_t kPlay = 0xB400;
const uint16_t kIsr = 0xB403;

const uint16_t kInhibit = 0xE5C1;
const uint16_t kCanAdd = 0xE5C2;
const uint16_t kSoundNumber = 0xE5C0;

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
Call(Machine1& machine, uint16_t address, int watch = -1)
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
	r.pc = address;

	for (int step = 0; step < 500000; step++) {
		if (watch >= 0) {
			// Watch the bytes a sequence writes rather than the code that
			// writes them: the difference is real behaviour, not a replay.
			const uint16_t pc = r.pc;
			static const uint16_t kWatch[4] = { 0xE5FC, 0xE5F7, 0xE5E1, 0xE5C3 };
			uint8_t before[4];
			for (int i = 0; i < 4; i++)
				before[i] = machine.Ram()[kWatch[i]];
			machine.StepInstruction();
			for (int i = 0; i < 4; i++) {
				if (machine.Ram()[kWatch[i]] == before[i])
					continue;
				printf("    t%d pc %04x: %04x %02x -> %02x\n", watch, pc,
					kWatch[i], before[i], machine.Ram()[kWatch[i]]);
			}
			if (r.pc == kSentinel)
				return true;
			continue;
		}
		machine.StepInstruction();
		if (r.pc == kSentinel)
			return true;
	}
	return false;
}

}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: vm_test <rom> [ticks] [codes]\n");
		return 1;
	}

	std::vector<uint8_t> bios;
	std::vector<uint8_t> logo;
	std::vector<uint8_t> rom;
	if (!ReadFile("third_party/cbios/cbios_main_msx1.rom", bios)) {
		fprintf(stderr, "cannot read the C-BIOS\n");
		return 1;
	}
	if (!ReadFile(argv[1], rom)) {
		fprintf(stderr, "cannot read the ROM %s\n", argv[1]);
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

	const int ticks = argc > 2 ? atoi(argv[2]) : 20;
	const int codes = argc > 3 ? atoi(argv[3]) : 256;
	const int watchTick = getenv("WTICK") != NULL ? atoi(getenv("WTICK")) : -1;
	int compared = 0;
	int failed = 0;
	int skipped = 0;

	for (int code = 0; code < codes; code++) {
		std::vector<uint8_t> start = base;
		start[kInhibit] = 0;
		start[kCanAdd] = 1;
		start[kSoundNumber] = static_cast<uint8_t>(code);

		memcpy(machine.Ram(), start.data(), start.size());
		if (!Call(machine, kPlay)) {
			printf("  code %d: PLAY_SOUND did not return\n", code);
			failed++;
			continue;
		}

		std::vector<uint8_t> initial(0x10000);
		memcpy(initial.data(), machine.Ram(), initial.size());
		uint8_t startRegs[16];
		for (int reg = 0; reg < 16; reg++)
			startRegs[reg] = machine.GetPsg().Register(reg);

		State state;
		memcpy(state.Ram(), initial.data(), initial.size());
		SoundPlayer player(state);
		for (int reg = 0; reg < 16; reg++)
			player.SetPsg(reg, startRegs[reg]);

		// Only the codes whose sequence lives in the cartridge are compared.
		// A descriptor byte that is not really a sound (the table is shorter
		// than 256 entries) can point anywhere, including into the BIOS, and
		// there the Z80 and the port are not looking at the same memory: the
		// port has the cartridge and nothing else. Every code the game really
		// plays stays inside the cartridge.
		// Every stream the code started has to be in the cartridge, not just
		// the last one: a two-voice code can have one voice in the table and
		// the other in a descriptor byte that is not a sound at all.
		bool outside = false;
		for (int index = 0; index < 2; index++) {
			const uint16_t stream = index == 0 ? 0xE5D3 : 0xE5E9;
			if (state.Ram()[stream] == 0)
				continue;
			const uint16_t sequence = static_cast<uint16_t>(
				state.Ram()[stream + 7] | (state.Ram()[stream + 8] << 8));
			if (sequence < 0x4000 || sequence >= 0xc000)
				outside = true;
		}
		if (outside) {
			skipped++;
			continue;
		}

		// Both sides play the same ticks from the same state.
		memcpy(machine.Ram(), initial.data(), initial.size());
		for (int reg = 0; reg < 16; reg++)
			machine.GetPsg().Write(reg, startRegs[reg]);

		for (int tick = 1; tick <= ticks; tick++) {
			if (!Call(machine, kIsr, tick == watchTick ? tick : -1)) {
				printf("  code %d tick %d: the original did not return\n",
					code, tick);
				failed++;
				break;
			}
			player.Tick();
			compared++;

			bool differs = false;
			for (int reg = 0; reg < 16; reg++) {
				const uint8_t expected = machine.GetPsg().Register(reg);
				const uint8_t got = player.ReadPsg(reg);
				if (expected == got)
					continue;
				if (failed < 12)
					printf("  code %d tick %d: psg %d original %02x,"
						" port %02x\n", code, tick, reg, expected, got);
				failed++;
				differs = true;
				break;
			}
			if (differs)
				break;

			for (uint16_t address = kComparedFrom; address < kComparedTo;
					address++) {
				if (machine.Ram()[address] == state.Ram()[address])
					continue;
				if (failed < 12)
					printf("  code %d tick %d: 0x%04x original %02x,"
						" port %02x\n", code, tick, address,
						machine.Ram()[address], state.Ram()[address]);
				if (failed < 2) {
					printf("    original:");
					for (int i = 0; i < 0x26; i++)
						printf(" %02x",
							machine.Ram()[0xE5C0 + i]);
					printf("\n    port    :");
					for (int i = 0; i < 0x26; i++)
						printf(" %02x", state.Ram()[0xE5C0 + i]);
					printf("\n");
				}
				failed++;
				differs = true;
				break;
			}
			if (differs)
				break;
		}
	}

	printf("%d of %d sound ticks agree with the original", compared - failed,
		compared);
	if (skipped > 0)
		printf(" (%d codes skipped: their sequence is outside the cartridge)",
			skipped);
	printf("\n");
	return failed == 0 ? 0 : 1;
}
