/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Calls each command handler the way the dispatcher does.
//
//   ./handler_trace build/arkanoid.rom
//
// The handlers cannot be probed one by one in isolation - they share tails, and
// the preset engine discards its caller's return address on purpose - so this
// enters them exactly as the dispatcher does: a stream in IX, the command byte
// in BC, the parameter in A and E, and the handler address in HL. It then
// prints everything the handler changed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kSequence = 0xF100;	// scratch sequence in RAM
const uint16_t kStream = 0xE5D3;	// SOUND_BUFFER_1

const uint16_t kPrimaryTable = 0xB66E;
const uint16_t kSecondaryTable = 0xB666;
const uint16_t kHandlerBlock = 0xB676;

const uint16_t kWatchedFrom = 0xE5C0;
const uint16_t kWatchedTo = 0xE5FE;


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


// Runs one handler. Returns false if it did not come back.
bool
Call(Machine1& machine, uint16_t address, uint8_t param)
{
	Z80Regs& r = machine.Cpu().r;
	machine.Write(kSequence, 0x40);		// the byte after the command
	machine.Write(kSequence + 1, 0x41);
	machine.Write(kSequence + 2, 0x42);

	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.af = 0;
	r.hl = address;
	r.bc = kSequence;
	r.de = 0;
	r.ix = kStream;
	r.iy = 0;
	r.a = param;
	r.e = param;
	r.sp = 0xF000;
	r.sp -= 2;
	machine.Write(r.sp, static_cast<uint8_t>(kSentinel & 0xff));
	machine.Write(r.sp + 1, static_cast<uint8_t>(kSentinel >> 8));
	r.pc = address;

	for (int step = 0; step < 200000; step++) {
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
		fprintf(stderr, "usage: handler_trace <rom>\n");
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

	const char* names[2] = { "primary", "secondary" };
	for (int table = 0; table < 2; table++) {
		const uint16_t offsets = table == 0 ? kPrimaryTable : kSecondaryTable;
		for (int command = 0; command < 8; command++) {
			for (int param = 1; param <= 3; param++) {
				std::vector<uint8_t> initial = base;
				// Word zero has to be tellable apart from the frame's bytes.
				for (int i = 0; i < 0x3e; i++)
					initial[kWatchedFrom + i] = 0xee;
				initial[kStream + 0] = 1;	// active, so handlers can act
				memcpy(machine.Ram(), initial.data(), initial.size());

				const uint16_t address = static_cast<uint16_t>(kHandlerBlock
					+ machine.Read(offsets + command));
				const bool returned = Call(machine, address, param);

				printf("%s cmd %d param %d -> 0x%04x%s\n", names[table],
					command, param, address, returned ? "" : "  (no return)");
				for (int i = 0; i < 0x3e; i++) {
					const uint8_t got = machine.Ram()[kWatchedFrom + i];
					if (got == initial[kWatchedFrom + i])
						continue;
					printf("    0x%04x: %02x -> %02x\n",
						kWatchedFrom + i, initial[kWatchedFrom + i], got);
				}
			}
		}
	}

	return 0;
}
