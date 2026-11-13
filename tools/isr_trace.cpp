/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Traces the PSG writes of SOUND_ISR_UPDATE on the reference machine.
//
//   ./isr_trace build/arkanoid.rom
//
// The interpreter talks to the chip through `out (0xA0),reg` and
// `out (0xA1),value`, so running it a single instruction at a time and
// watching for those two opcodes gives the exact order of the writes, which is
// what a transcription needs and what the final state alone does not show.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kIsr = 0xB403;	// jp SOUND_ISR_UPDATE

const uint16_t kInhibit = 0xE5C1;
const uint16_t kRegMask = 0xE5C3;
const uint16_t kRegsBuffer = 0xE5C4;
const uint16_t kStream1 = 0xE5D3;
const uint16_t kStream2 = 0xE5E9;


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


// Runs one tick and prints every PSG register write in the order it happens.
bool
Trace(Machine1& machine, bool dumpRam)
{
	machine.GetPsg().Write(7, 0xbf);

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
	r.pc = kIsr;

	int pendingReg = -1;
	int writes = 0;

	for (int step = 0; step < 200000; step++) {
		const uint8_t opcode = machine.Read(r.pc);
		const uint8_t operand = machine.Read(r.pc + 1);
		if (opcode == 0xd3 && (operand == 0xa0 || operand == 0xa1)) {
			if (operand == 0xa0)
				pendingReg = r.a;
			else
				printf("    reg %2d = %02x\n", pendingReg, r.a);
			writes++;
		}
		machine.StepInstruction();
		if (r.pc == kSentinel)
			break;
	}

	if (dumpRam) {
		printf("    mask now %02x, regs buffer:", machine.Ram()[kRegMask]);
		for (int i = 0; i < 0x0e; i++)
			printf(" %02x", machine.Ram()[kRegsBuffer + i]);
		printf("\n");
	}

	return writes > 0;
}

}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: isr_trace <rom>\n");
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

	static uint8_t masks[2] = { 0x00, 0xff };
	for (int index = 0; index < 2; index++) {
		std::vector<uint8_t> initial = base;
		initial[kInhibit] = 0;
		initial[kRegMask] = masks[index];
		// Bit 7 set in every byte, so the mixer write happens, and bit 3 set
		// in the byte after the image, which is the last conditional write.
		for (int i = 0; i < 0x0f; i++)
			initial[kRegsBuffer + i] = static_cast<uint8_t>(0xc0 | i);

		// Both streams idle, so only the register image is written.
		initial[kStream1] = 0;
		initial[kStream2] = 0;

		memcpy(machine.Ram(), initial.data(), initial.size());
		printf("    mask %02x\n", masks[index]);
		Trace(machine, false);
	}

	return 0;
}
