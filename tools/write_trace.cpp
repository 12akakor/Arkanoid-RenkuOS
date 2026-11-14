/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Write-for-write comparison of one sound tick.
//
//   ./write_trace build/arkanoid.rom <code> <tick>
//
// Both sides start from the same memory, copied out of the reference machine at
// the top of the call, so any difference in the result is a difference in what
// the code does and not in what it was handed.
//
// The original's writes are collected by stepping instructions and watching the
// sound window change. The port's are collected through State::onWrite, which
// every write goes through. The two journals are then compared in order: the
// first place they disagree is the instruction (or the code path) to look at,
// with a few of the writes before it for context.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/SoundPlayer.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xBEEF;
const uint16_t kPlay = 0xB400;
const uint16_t kIsr = 0xB403;

const uint16_t kSoundFrom = 0xE5C0;
const uint16_t kSoundTo = 0xE600;

const uint16_t kInhibit = 0xE5C1;
const uint16_t kCanAdd = 0xE5C2;
const uint16_t kSoundNumber = 0xE5C0;


struct Write {
	uint16_t	address;
	uint8_t		value;
	uint16_t	pc;
};


bool
ReadFile(const char* path, std::vector<uint8_t>& data)
{
	FILE* file = fopen(path, "rb");
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


// Runs one routine to completion and reports whether it returned.
bool
Run(Machine1& machine, uint16_t address)
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
	machine.Write(--r.sp, static_cast<uint8_t>(kSentinel >> 8));
	machine.Write(--r.sp, static_cast<uint8_t>(kSentinel & 0xff));
	r.pc = address;

	for (int step = 0; step < 500000; step++) {
		machine.StepInstruction();
		if (r.pc == kSentinel)
			return true;
	}
	return false;
}


// Every change to the sound window while one call runs, in order, with the
// address of the instruction that made it.
std::vector<Write>
TraceOriginal(Machine1& machine)
{
	std::vector<Write> writes;

	Z80Regs& r = machine.Cpu().r;
	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.sp = 0xF000;
	machine.Write(--r.sp, static_cast<uint8_t>(kSentinel >> 8));
	machine.Write(--r.sp, static_cast<uint8_t>(kSentinel & 0xff));
	r.pc = kIsr;

	std::vector<uint8_t> before(kSoundTo - kSoundFrom);
	memcpy(before.data(), machine.Ram() + kSoundFrom, before.size());

	for (int step = 0; step < 500000; step++) {
		const uint16_t pc = r.pc;
		machine.StepInstruction();
		for (uint16_t address = kSoundFrom; address < kSoundTo; address++) {
			const uint8_t value = machine.Ram()[address];
			if (value == before[address - kSoundFrom])
				continue;
			before[address - kSoundFrom] = value;
			writes.push_back({ address, value, pc });
		}
		if (r.pc == kSentinel)
			break;
	}
	return writes;
}

}


int
main(int argc, char** argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: write_trace <rom> <code> <tick>\n");
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

	const int code = atoi(argv[2]);
	const int tick = atoi(argv[3]);

	machine.Ram()[kInhibit] = 0;
	machine.Ram()[kCanAdd] = 1;
	machine.Ram()[kSoundNumber] = static_cast<uint8_t>(code);
	if (!Run(machine, kPlay)) {
		fprintf(stderr, "PLAY_SOUND did not return\n");
		return 1;
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

	// Up to the tick in question both sides run the same thing, so the port
	// follows the original frame for frame.
	for (int frame = 0; frame <= tick; frame++) {
		if (frame < tick) {
			Run(machine, kIsr);
			player.Tick();
			// The port does not model the game's own RAM, and these codes
			// keep their sequences there (0x2722 for code 109), so the
			// reference machine's RAM is handed over each tick and only the
			// sound window is asserted.
			memcpy(state.Ram(), machine.Ram(), 0x10000);
			continue;
		}

		// The state the original is handed at the top of the call is also
		// the state the port starts from, byte for byte. Passing "nosync"
		// keeps the port's own state, which is what shows where the two
		// have drifted apart before the call.
		const bool sync = argc < 5 || strcmp(argv[4], "nosync") != 0;
		if (sync) {
			memcpy(state.Ram(), machine.Ram(), 0x10000);
		} else {
			int differing = 0;
			for (int address = 0; address < 0x10000; address++) {
				if (state.Ram()[address] == machine.Ram()[address])
					continue;
				if (differing++ >= 8)
					continue;
				printf("  ram 0x%04x original %02x, port %02x\n", address,
					machine.Ram()[address], state.Ram()[address]);
			}
			if (differing > 8)
				printf("  ... %d more bytes differ\n", differing - 8);
		}

		const std::vector<Write> original = TraceOriginal(machine);

		std::vector<Write> port;
		// The original's journal only holds changes, because that is what
		// stepping instructions can see. The port reports every write, so
		// the ones that change nothing are dropped here as well.
		std::vector<uint8_t> shadow(kSoundTo - kSoundFrom);
		memcpy(shadow.data(), state.Ram() + kSoundFrom, shadow.size());
		state.onWrite = [&port, &shadow](uint16_t address, uint8_t value) {
			if (address < kSoundFrom || address >= kSoundTo)
				return;
			if (shadow[address - kSoundFrom] == value)
				return;
			shadow[address - kSoundFrom] = value;
			port.push_back({ address, value, 0 });
		};
		player.Tick();
		state.onWrite = NULL;

		printf("code %d tick %d: %d writes in the original, %d in the port\n",
			code, tick, (int)original.size(), (int)port.size());

		int i = 0;
		while (i < (int)original.size() && i < (int)port.size()
				&& original[i].address == port[i].address
				&& original[i].value == port[i].value)
			i++;

		if (i == (int)original.size() && i == (int)port.size()) {
			printf("  the same writes, in the same order\n");
			return 0;
		}

		const int from = i > 3 ? i - 3 : 0;
		printf("  first difference at write %d\n", i);
		printf("  %-8s %-10s %-10s\n", "n", "original", "port");
		for (int n = from; n < i + 4; n++) {
			char left[32] = "-";
			char right[32] = "-";
			if (n < (int)original.size())
				snprintf(left, sizeof(left), "%04x=%02x @%04x",
					original[n].address, original[n].value, original[n].pc);
			if (n < (int)port.size())
				snprintf(right, sizeof(right), "%04x=%02x",
					port[n].address, port[n].value);
			printf("  %-8d %-10s %-10s%s\n", n, left, right,
				n == i ? "  <-" : "");
		}
		return 1;
	}

	return 0;
}
