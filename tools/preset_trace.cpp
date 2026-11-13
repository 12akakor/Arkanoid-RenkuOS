/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Shows what INIT_EFFECT_FROM_PRESET (0xB7E6) does to an effect state.
//
//   ./preset_trace build/arkanoid.rom
//
// The routine uses the stack as control flow - it is called, then calls the
// step loader, which pops its own return address - so the only safe way to
// learn what it leaves behind is to run it and look.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kInit = 0xB7E6;
const uint16_t kState = 0xE5E6;	// DELAYED_EFFECT_STATE_A_STREAM1
const uint16_t kPresetTable = 0xB4AE;


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
Call(Machine1& machine, uint16_t address, uint16_t de, uint8_t a, uint8_t& outE)
{
	Z80Regs& r = machine.Cpu().r;
	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.af = 0;
	r.hl = 0;
	r.bc = 0;
	r.de = de;
	r.ix = 0;
	r.iy = 0;
	r.a = a;
	r.sp = 0xF000;
	r.sp -= 2;
	machine.Write(r.sp, static_cast<uint8_t>(kSentinel & 0xff));
	machine.Write(r.sp + 1, static_cast<uint8_t>(kSentinel >> 8));
	r.pc = address;

	int traced = 0;
	for (int step = 0; step < 200000; step++) {
		if (getenv("TRACE_PC") != NULL && traced < 12) {
			printf("    pc %04x a %02x de %04x hl %04x sp %04x\n",
				r.pc, r.a, r.de, r.hl, r.sp);
			traced++;
		}
		machine.StepInstruction();
		if (r.pc == kSentinel) {
			outE = r.e;
			return true;
		}
	}
	return false;
}

}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: preset_trace <rom>\n");
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

	printf("preset table entries (index: byte at 0xB4AE+index)\n");
	for (int index = 0; index < 24; index++) {
		printf("  %2d: %02x\n", index,
			machine.Read(kPresetTable + index));
	}

	for (int preset = 0; preset < 24; preset++) {
		std::vector<uint8_t> before(0x10000);
		memcpy(before.data(), machine.Ram(), before.size());
		machine.Write(kState, static_cast<uint8_t>(preset));
		// Clear the bytes below the state so the writes are visible.
		for (int i = 1; i <= 8; i++)
			machine.Write(kState - i, 0xee);

		uint8_t outE = 0;
		if (!Call(machine, kInit, kState, 0, outE)) {
			printf("repr %d: did not return\n", preset);
			continue;
		}
		printf("state[0]=%d -> e=%02x  bytes:", preset, outE);
		for (int i = 6; i >= 1; i--)
			printf(" [%d]=%02x", i, machine.Ram()[kState - i]);
		printf("  [0]=%02x\n", machine.Ram()[kState]);
	}

	return 0;
}
