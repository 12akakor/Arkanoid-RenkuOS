/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Runs the original's sound interpreter for many ticks and prints what it did.
//
//   ./isr_multi build/arkanoid.rom <sound code> [ticks]
//
// The one-tick harness (isr_trace) cannot show a bug that lives across ticks: a
// countdown off by one, a stream pointer stored back a byte late. This prints,
// for each tick, both streams, the channel mask and the registers, so a
// transcription can be compared line by line against what really happened.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kPlay = 0xB400;	// jp PLAY_SOUND
const uint16_t kIsr = 0xB403;	// jp SOUND_ISR_UPDATE

const uint16_t kInhibit = 0xE5C1;
const uint16_t kCanAdd = 0xE5C2;
const uint16_t kRegMask = 0xE5C3;
const uint16_t kRegsBuffer = 0xE5C4;
const uint16_t kSoundNumber = 0xE5C0;
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


bool
Call(Machine1& machine, uint16_t address)
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
		machine.StepInstruction();
		if (r.pc == kSentinel)
			return true;
	}
	return false;
}


void
Dump(Machine1& machine, const char* label, int tick)
{
	printf("%-6s %3d  s1", label, tick);
	for (int i = 0; i < 10; i++)
		printf(" %02x", machine.Ram()[kStream1 + i]);
	printf("  s2");
	for (int i = 0; i < 10; i++)
		printf(" %02x", machine.Ram()[kStream2 + i]);
	printf("\n            mask %02x  regs", machine.Ram()[kRegMask]);
	for (int reg = 0; reg < 14; reg++)
		printf(" %02x", machine.GetPsg().Register(reg));
	printf("\n");
}

}


int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: isr_multi <rom> <sound code> [ticks]\n");
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

	const uint8_t code = static_cast<uint8_t>(atoi(argv[2]));
	const int ticks = argc > 3 ? atoi(argv[3]) : 12;
	machine.Write(kInhibit, 0);
	machine.Write(kCanAdd, 1);
	machine.Write(kSoundNumber, code);
	Call(machine, kPlay);

	printf("sound %d, as played\n", code);
	Dump(machine, "play", 0);

	for (int tick = 1; tick <= ticks; tick++) {
		Call(machine, kIsr);
		Dump(machine, "tick", tick);
	}

	return 0;
}
