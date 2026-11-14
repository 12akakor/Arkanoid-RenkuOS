/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// What CONFIGURE_CHANNEL_ENABLES (0xB831) really does.
//
//   ./mixer_trace build/arkanoid.rom <command> <e> <d> <voice>
//
// The routine is called with BC on the command byte, E and D as the handler
// leaves them, and the voice control byte already in 0xE5CB. It either returns
// to its caller (the handler keeps going) or does `pop af; ret` and so returns
// one level higher. Two sentinels on the stack tell the two apart, and every
// instruction's A, H, L and carry are printed so the shifts can be read off the
// real processor instead of from the listing.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinelReturn = 0xBEEF;	// the caller's return address
const uint16_t kSentinelSkip = 0xBEE0;		// what `pop af; ret` lands on
const uint16_t kRoutine = 0xB831;

const uint16_t kCommandAddress = 0xF100;
const uint16_t kVoiceControl = 0xE5CB;


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

}


int
main(int argc, char** argv)
{
	if (argc < 6) {
		fprintf(stderr, "usage: mixer_trace <rom> <command> <e> <d> <voice>\n");
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

	const uint8_t command = static_cast<uint8_t>(strtoul(argv[2], NULL, 16));
	const uint8_t e = static_cast<uint8_t>(strtoul(argv[3], NULL, 16));
	const uint8_t d = static_cast<uint8_t>(strtoul(argv[4], NULL, 16));
	const uint8_t voice = static_cast<uint8_t>(strtoul(argv[5], NULL, 16));

	machine.Ram()[kCommandAddress] = command;
	machine.Ram()[kVoiceControl] = voice;

	Z80Regs& r = machine.Cpu().r;
	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.af = 0;
	r.bc = kCommandAddress;
	r.de = static_cast<uint16_t>((d << 8) | e);
	r.hl = 0;
	r.ix = 0;
	r.iy = 0;

	// Stack: sentinel2 below sentinel1, so a `pop af; ret` eats the first and
	// returns to the second.
	r.sp = 0xF000;
	machine.Write(--r.sp, static_cast<uint8_t>(kSentinelSkip >> 8));
	machine.Write(--r.sp, static_cast<uint8_t>(kSentinelSkip & 0xff));
	machine.Write(--r.sp, static_cast<uint8_t>(kSentinelReturn >> 8));
	machine.Write(--r.sp, static_cast<uint8_t>(kSentinelReturn & 0xff));
	r.pc = kRoutine;

	printf("command %02x  e %02x  d %02x  voice %02x\n", command, e, d, voice);
	for (int step = 0; step < 2000; step++) {
		const uint16_t pc = r.pc;
		machine.StepInstruction();
		printf("  %04x: a %02x f %02x h %02x l %02x carry %d\n", pc, r.a,
			r.f & 0xff, r.h, r.l, (r.f & 0x01) != 0);
		if (r.pc == kSentinelReturn) {
			printf("  returns to the handler; voice now %02x\n",
				machine.Ram()[kVoiceControl]);
			return 0;
		}
		if (r.pc == kSentinelSkip) {
			printf("  returns past the handler; voice now %02x\n",
				machine.Ram()[kVoiceControl]);
			return 0;
		}
	}

	printf("  did not return\n");
	return 1;
}
