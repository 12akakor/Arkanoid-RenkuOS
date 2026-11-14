/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// What INIT_EFFECT (0xB7E6) does with a preset, and which bytes it touches.
//
//   ./effect_trace build/arkanoid.rom <preset>
//
// The preset command calls it as `ld de,0xE5FC; call 0xB7E6`, so DE is the
// address of the preset byte and everything else is left over from the handler.
// Every instruction's registers and every change to the effect state bytes are
// printed, which says whether the stream's effect state is written here at all.
#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xBEEF;
const uint16_t kRoutine = 0xB7E6;
const uint16_t kPresetAddress = 0xE5FC;

const uint16_t kWatched[3] = { 0xE5F6, 0xE5F7, 0xE5CE };


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

}


int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: effect_trace <rom> <preset>\n");
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

	const uint8_t preset = static_cast<uint8_t>(strtoul(argv[2], NULL, 16));
	machine.Ram()[kPresetAddress] = preset;

	Z80Regs& r = machine.Cpu().r;
	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.af = 0;
	r.bc = 0;
	r.de = kPresetAddress;
	r.hl = 0;
	r.ix = 0;
	r.iy = 0;
	r.sp = 0xF000;
	machine.Write(--r.sp, static_cast<uint8_t>(kSentinel >> 8));
	machine.Write(--r.sp, static_cast<uint8_t>(kSentinel & 0xff));
	r.pc = kRoutine;

	printf("preset %02x\n", preset);
	for (int step = 0; step < 4000; step++) {
		const uint16_t pc = r.pc;
		uint8_t before[3];
		for (int i = 0; i < 3; i++)
			before[i] = machine.Ram()[kWatched[i]];

		machine.StepInstruction();

		printf("  %04x: a %02x h %02x l %02x e %02x carry %d", pc, r.a, r.h,
			r.l, r.e, (r.f & 0x01) != 0);
		for (int i = 0; i < 3; i++) {
			if (machine.Ram()[kWatched[i]] != before[i]) {
				printf("  %04x %02x->%02x", kWatched[i], before[i],
					machine.Ram()[kWatched[i]]);
			}
		}
		printf("\n");

		if (r.pc == kSentinel) {
			printf("  returns; preset %02x state %02x %02x\n",
				machine.Ram()[kPresetAddress], machine.Ram()[kWatched[0]],
				machine.Ram()[kWatched[1]]);
			return 0;
		}
	}

	printf("  did not return\n");
	return 1;
}
