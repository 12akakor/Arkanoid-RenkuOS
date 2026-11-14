/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Which handler each command index really runs.
//
//   ./table_trace build/arkanoid.rom
//
// The offset tables at 0xB666 and 0xB66E are hard to read by hand — some of the
// addresses they resolve to are stubs that fall into another handler — so this
// runs DISPATCH_PRIMARY (0xB63E) and DISPATCH_SECONDARY (0xB649) on the reference
// machine, one command byte at a time, and prints the first address the dispatcher
// jumps to outside itself. That address is the entry, whatever the table says.
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xBEEF;
const uint16_t kSequence = 0xF200;

const uint16_t kDispatchPrimary = 0xB63E;
const uint16_t kDispatchSecondary = 0xB649;
const uint16_t kDispatcherStart = 0xB63E;
const uint16_t kDispatcherEnd = 0xB666;


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


uint16_t
Entry(Machine1& machine, uint16_t dispatcher, uint8_t command)
{
	// The dispatcher starts with `inc bc`, so BC points one below the command.
	machine.Ram()[kSequence - 1] = 0x00;
	machine.Ram()[kSequence] = command;
	machine.Ram()[kSequence + 1] = 0x00;	// a wait for anything that asks
	machine.Ram()[kSequence + 2] = 0x00;
	machine.Ram()[kSequence + 3] = 0x00;

	Z80Regs& r = machine.Cpu().r;
	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.af = 0;
	r.bc = kSequence - 1;
	r.de = 0;
	r.hl = 0;
	r.ix = 0;
	r.iy = 0;
	r.sp = 0xF000;
	machine.Write(--r.sp, static_cast<uint8_t>(kSentinel >> 8));
	machine.Write(--r.sp, static_cast<uint8_t>(kSentinel & 0xff));
	r.pc = dispatcher;

	for (int step = 0; step < 64; step++) {
		machine.StepInstruction();
		const uint16_t pc = r.pc;
		if (pc < kDispatcherStart || pc >= kDispatcherEnd)
			return pc;
	}
	return 0xffff;
}

}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: table_trace <rom>\n");
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

	printf("index  primary  secondary\n");
	for (int index = 0; index < 8; index++) {
		// A command with that index, bit 7 set, no low nibble.
		const uint8_t command = static_cast<uint8_t>(0x80 | (index << 4));
		const uint16_t primary = Entry(machine, kDispatchPrimary, command);
		const uint16_t secondary = Entry(machine, kDispatchSecondary, command);
		printf("  %d     %04x     %04x\n", index, primary, secondary);
	}
	return 0;
}
