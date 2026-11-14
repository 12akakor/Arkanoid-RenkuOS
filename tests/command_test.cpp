/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of the command dispatcher, one command at a time.
//
//   ./command_test build/arkanoid.rom [cases]
//
// A fake sequence lives in RAM: one command byte, two parameter bytes, then a
// byte with bit 7 clear which ends the dispatch. The original's dispatcher and
// the port's are given the same state and must come back with the same memory
// and the same stream pointer.
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
const uint16_t kPrimary = 0xB63E;	// DISPATCH_PRIMARY_SOUND_COMMAND
const uint16_t kSecondary = 0xB649;

const uint16_t kSequence = 0xF100;
const uint16_t kStream = 0xE5D3;

const uint16_t kComparedFrom = 0xE001;
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
CallOriginal(Machine1& machine, uint16_t address, uint16_t pointer,
	uint16_t& endPointer, int slot)
{
	Z80Regs& r = machine.Cpu().r;
	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.af = 0;
	r.hl = 0;
	r.bc = pointer;
	r.de = 0;
	r.ix = kStream;
	r.iy = 0;
	r.sp = 0xF000;
	r.sp -= 2;
	machine.Write(r.sp, static_cast<uint8_t>(kSentinel & 0xff));
	machine.Write(r.sp + 1, static_cast<uint8_t>(kSentinel >> 8));
	r.pc = address;

	int traced = 0;
	for (int step = 0; step < 200000; step++) {
		if (getenv("TRACE") != NULL && traced < 24) {
			printf("      pc %04x bc %04x hl %04x a %02x\n", r.pc, r.bc,
				r.hl, r.a);
			traced++;
		}
		machine.StepInstruction();
		if (r.pc == kSentinel) {
			// The dispatcher does not return the pointer: its end path stores
			// it and falls into the rest of the tick. Read the stored one.
			const uint16_t address = slot == 0 ? 0xE5DA : 0xE5F0;
			endPointer = static_cast<uint16_t>(
				machine.Ram()[address] | (machine.Ram()[address + 1] << 8));
			return true;
		}
	}
	return false;
}


struct Generator {
	uint32_t	state = 99;

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
		fprintf(stderr, "usage: command_test <rom> [cases]\n");
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
	int perCommand[16] = { 0 };
	int shownPerCommand[16] = { 0 };

	for (int index = 0; index < cases; index++) {
		std::vector<uint8_t> initial = base;

		const bool primaryTable = (index & 1) == 0;
		const uint8_t command = static_cast<uint8_t>(
			0x80 | (generator.Range(0, 7) << 4) | generator.Range(0, 15));
		const uint8_t one = static_cast<uint8_t>(generator.Range(0, 255));
		const uint8_t two = static_cast<uint8_t>(generator.Range(0, 255));

		initial[kSequence] = command;
		initial[kSequence + 1] = one;
		initial[kSequence + 2] = two;
		// Everything after the parameters is zero, so however many bytes the
		// handler consumes, the dispatch ends on one of them and the pointer
		// says how far it got.
		for (int i = 3; i < 0x24; i++)
			initial[kSequence + i] = 0;

		// A stream that is already doing something.
		for (int i = 0; i < 0x20; i++)
			initial[kStream + i] = static_cast<uint8_t>(
				generator.Range(0, 255));

		memcpy(machine.Ram(), initial.data(), initial.size());
		uint16_t expectedEnd = 0;
		if (!CallOriginal(machine, primaryTable ? kPrimary : kSecondary,
				kSequence - 1, expectedEnd, primaryTable ? 0 : 1)) {
			printf("  case %d: the original did not return\n", index);
			failed++;
			continue;
		}
		std::vector<uint8_t> expected(0x10000);
		memcpy(expected.data(), machine.Ram(), expected.size());

		State state;
		memcpy(state.Ram(), initial.data(), initial.size());
		SoundPlayer player(state);
		const uint16_t end = player.Dispatch(kStream, kSequence - 1,
			primaryTable);

		compared++;

		const int key = ((primaryTable ? 0 : 1) << 3)
			| ((command >> 4) & 7);
		perCommand[key]++;

		bool differs = false;
		if (end != expectedEnd) {
			if (shownPerCommand[key]++ < 2)
				printf("  case %d cmd %02x (%s): pointer original %04x,"
					" port %04x\n", index, command,
					primaryTable ? "primary" : "secondary", expectedEnd,
					end);
			failed++;
			continue;
		}

		for (uint16_t address = kComparedFrom; address < kComparedTo;
				address++) {
			if (expected[address] == state.Ram()[address])
				continue;
			if (shownPerCommand[key]++ < 2)
				printf("  case %d cmd %02x (%s): 0x%04x original %02x,"
					" port %02x\n", index, command,
					primaryTable ? "primary" : "secondary", address,
					expected[address], state.Ram()[address]);
			failed++;
			differs = true;
			break;
		}
		if (differs)
			continue;
	}

	printf("%d of %d commands agree with the original\n", compared - failed,
		compared);
	for (int key = 0; key < 16; key++) {
		if (perCommand[key] == 0)
			continue;
		printf("  %-9s cmd %d: %d cases\n",
			(key & 8) ? "secondary" : "primary ", key & 7, perCommand[key]);
	}
	return failed == 0 ? 0 : 1;
}
