/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// DECOMPRESS_TILE_COLORS, checked against the cartridge.
//
//   ./tile_colors_test build/arkanoid.rom
//
// The routine is called on the reference machine with the compressed stream and
// the destination the game gives it - the title's colours at 0x93F4 and the
// in-game colours at 0x8584, each into the three thirds of the colour table -
// and the port's version is run on the same stream. Every byte from 0x2000 to
// 0x3800 has to match, including anything written past the 0x800-byte limit:
// the pair loop can run past it before the limit is looked at again.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/RomImage.h"
#include "game/Screen.h"
#include "game/TileColors.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;


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


// DECOMPRESS_TILE_COLORS with DE and HL set, run until it returns.
bool
CallOriginal(Machine1& machine, uint16_t compressed, uint16_t vramAddress)
{
	Z80Regs& r = machine.Cpu().r;
	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.de = compressed;
	r.hl = vramAddress;
	r.af = 0;
	r.bc = 0;
	r.sp = 0xF000 - 2;
	machine.Write(r.sp, static_cast<uint8_t>(kSentinel & 0xFF));
	machine.Write(r.sp + 1, static_cast<uint8_t>(kSentinel >> 8));
	r.pc = 0x4389;

	for (int step = 0; step < 3000000; step++) {
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
		fprintf(stderr, "usage: tile_colors_test <rom>\n");
		return 1;
	}
	std::vector<uint8_t> bios, logo, rom;
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

	// The machine is stepped instruction by instruction below, and it needs a
	// running game before its first instruction is the one put in PC: after a
	// bare Reset the call never returns.
	for (int frame = 0; frame < 600; frame++)
		machine.RunFrame();

	printf("DECOMPRESS_TILE_COLORS, against the cartridge\n");

	struct Case {
		const char*	name;
		uint16_t	compressed;
	};

	const Case cases[] = {
		{ "title colours", 0x93F4 },
		{ "in-game colours", 0x8584 }
	};

	int failed = 0;
	int compared = 0;

	for (const Case& test : cases) {
		const uint16_t thirds[3] = { 0x2000, 0x2800, 0x3000 };
		for (int third = 0; third < 3; third++) {
			memset(machine.Vdp().VramWritable(), 0, 0x4000);
			if (!CallOriginal(machine, test.compressed, thirds[third])) {
				printf("  %s at %04x: the original did not return\n",
					test.name, thirds[third]);
				failed++;
				compared++;
				continue;
			}

			Screen screen;
			memset(screen.VramPointer(), 0, 0x4000);
			const int written = TileColors::Decompress(
				kRomImage + (test.compressed - 0x4000), screen, thirds[third]);

			int different = 0;
			uint16_t firstDifference = 0;
			for (uint16_t address = 0x2000; address < 0x3800; address++) {
				if (screen.VramPointer()[address]
						== machine.Vdp().Vram()[address])
					continue;
				if (different == 0)
					firstDifference = address;
				different++;
			}

			printf("  %-16s at %04x: %d bytes written, %d differ\n",
				test.name, thirds[third], written, different);
			if (different != 0)
				printf("    first at %04x\n", firstDifference);
			failed += different == 0 ? 0 : 1;
			compared++;
		}
	}

	printf("%d of %d tables agree\n", compared - failed, compared);
	return failed == 0 ? 0 : 1;
}
