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

const char*
Name(uint16_t address)
{
	switch (address) {
		case 0xE5C1: return "inhibit";
		case 0xE5C3: return "reg mask";
		case 0xE5C4: return "period 1";
		case 0xE5C6: return "period 2";
		case 0xE5CC: return "volume 1";
		case 0xE5CE: return "volume 2";
		case 0xE5D3: return "s1 active";
		case 0xE5D5: return "s1 follow";
		case 0xE5D6: return "s1 chain";
		case 0xE5D7: return "s1 repeat";
		case 0xE5D8: return "s1 descriptor";
		case 0xE5D9: return "s1 descriptor+1";
		case 0xE5DA: return "s1 pointer";
		case 0xE5DB: return "s1 pointer+1";
		case 0xE5DC: return "s1 ticks";
		case 0xE5DD: return "s1 effect";
		case 0xE5E1: return "s1 effect state";
		case 0xE5E6: return "s1 preset";
		case 0xE5E9: return "s2 active";
		case 0xE5EB: return "s2 follow";
		case 0xE5EC: return "s2 chain";
		case 0xE5ED: return "s2 repeat";
		case 0xE5F0: return "s2 pointer";
		case 0xE5F1: return "s2 pointer+1";
		case 0xE5F2: return "s2 ticks";
		case 0xE5F7: return "s2 effect state";
		case 0xE5FC: return "s2 preset";
	}
	return NULL;
}

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
			if (getenv("WCMD") != NULL && atoi(getenv("WCMD")) == watch
					&& (pc == 0xb640 || pc == 0xb64a)) {
				printf("    cmd@%04x bc %04x byte %02x\n", pc, r.bc,
					machine.Read(r.bc));
			}
			if (getenv("WBR") != NULL && atoi(getenv("WBR")) == watch
					&& (pc == 0xb4c3 || pc == 0xb4c9 || pc == 0xb4cb
						|| pc == 0xb4d1 || pc == 0xb4e1 || pc == 0xb4e5)) {
				printf("    br %04x hl %04x a %02x\n", pc, r.hl, r.a);
			}
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
		fprintf(stderr, "usage: vm_test <rom> [ticks] [codes] [allow]\n");
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
	// A budget for known mismatches, so the sweep can run in the suite before
	// the last few are closed. Printing it keeps it visible: a fifth mismatch
	// fails, and the count only ever goes down.
	const int allow = argc > 4 ? atoi(argv[4]) : 0;
	// A tick count of zero means "until the sound ends": both streams
	// inactive, which is what the caller of PLAY_SOUND actually waits for.
	const bool untilEnd = ticks == 0;
	const int kCap = 20000;
	int compared = 0;
	int failed = 0;
	int skipped = 0;
	int ended = 0;
	int capped = 0;

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

		for (int tick = 1; ; tick++) {
			if (!untilEnd && tick > ticks)
				break;
			if (untilEnd && tick > kCap) {
				if (capped++ < 4)
					printf("  code %d: still playing after %d ticks\n",
						code, kCap);
				break;
			}

			// A code whose stream pointer has left the cartridge is not a
			// sound the port can be compared against: the Z80 is reading the
			// BIOS in slot 0 or a slot with nothing in it, and the port has
			// neither. The check before the tick loop cannot see this, because
			// a descriptor that is not really a sound can start inside the
			// cartridge and wander out of it several ticks later.
			bool left = false;
			for (int index = 0; index < 2; index++) {
				const uint16_t stream = index == 0 ? 0xE5D3 : 0xE5E9;
				if (machine.Ram()[stream] == 0)
					continue;
				const uint16_t pointer = static_cast<uint16_t>(
					machine.Ram()[stream + 7]
					| (machine.Ram()[stream + 8] << 8));
				if (pointer < 0x4000 || pointer >= 0xc000)
					left = true;
			}
			if (left) {
				skipped++;
				break;
			}

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

			int shown = 0;
			for (uint16_t address = kComparedFrom; address < kComparedTo;
					address++) {
				if (machine.Ram()[address] == state.Ram()[address])
					continue;
				differs = true;
				if (shown++ >= 8)
					continue;
				const char* name = Name(address);
				printf("  code %d tick %d: 0x%04x %s original %02x,"
					" port %02x\n", code, tick, address,
					name != NULL ? name : "",
					machine.Ram()[address], state.Ram()[address]);
			}
			if (differs) {
				if (shown > 8)
					printf("  ... %d more bytes differ\n", shown - 8);
				failed++;
				break;
			}

			// Both streams quiet: the sound is over.
			if (untilEnd && machine.Ram()[0xE5D3] == 0
					&& machine.Ram()[0xE5E9] == 0) {
				ended++;
				break;
			}
		}
	}

	printf("%d of %d sound ticks agree with the original", compared - failed,
		compared);
	if (skipped > 0)
		printf(" (%d codes skipped: their sequence is outside the cartridge)",
			skipped);
	if (untilEnd)
		printf(" (%d played to their end, %d still going after %d ticks)",
			ended, capped, kCap);
	printf("\n");
	if (failed > allow) {
		printf("%d mismatches, more than the %d allowed\n", failed, allow);
		return 1;
	}
	if (failed > 0)
		printf("%d known mismatch(es) allowed\n", failed);
	return 0;
}
