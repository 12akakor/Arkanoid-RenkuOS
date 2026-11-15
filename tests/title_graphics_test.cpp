/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The title screen's graphics, from the cartridge into working VRAM.
//
//   ./title_graphics_test build/arkanoid.rom
//
// Two routines put the title on the screen before DRAW_TITLE_SCREEN even runs:
// LDIRVM_32x24_THIRD (0x4220) copies a third of the pattern table straight from
// the cartridge, three times, and FILL_COLORS_ALL_SCREEN (0x41FF) expands the
// title's colours into the three thirds of the colour table. Both are run on
// the reference machine and in the port from the same data, and every byte of
// the pattern and colour tables has to agree.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/RomImage.h"
#include "game/Screen.h"
#include "game/VdpBlocks.h"
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


// One cartridge routine with HL and DE set, run until it returns.
bool
CallOriginal(Machine1& machine, uint16_t routine, uint16_t hl, uint16_t de)
{
	Z80Regs& r = machine.Cpu().r;
	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.hl = hl;
	r.de = de;
	r.af = 0;
	r.bc = 0;
	r.sp = 0xF000 - 2;
	machine.Write(r.sp, static_cast<uint8_t>(kSentinel & 0xFF));
	machine.Write(r.sp + 1, static_cast<uint8_t>(kSentinel >> 8));
	r.pc = routine;

	for (int step = 0; step < 3000000; step++) {
		machine.StepInstruction();
		if (r.pc == kSentinel)
			return true;
	}
	return false;
}


int
Compare(const char* name, Screen& screen, Machine1& machine, uint16_t from,
	uint16_t to)
{
	int different = 0;
	uint16_t first = 0;
	for (uint16_t address = from; address < to; address++) {
		if (screen.VramPointer()[address] == machine.Vdp().Vram()[address])
			continue;
		if (different == 0)
			first = address;
		different++;
	}
	printf("  %-28s %04x-%04x: %d differ", name, from, to, different);
	if (different != 0)
		printf(" (first %04x)", first);
	printf("\n");
	return different == 0 ? 0 : 1;
}

}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: title_graphics_test <rom>\n");
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
	// A cartridge routine stepped by instruction needs a running game first.
	for (int frame = 0; frame < 600; frame++)
		machine.RunFrame();

	printf("the title screen's graphics, against the cartridge\n");

	int failed = 0;

	// The pattern table: three thirds from one block, for the title's tiles and
	// the in-game ones.
	struct Block {
		const char*	name;
		uint16_t	source;
	};

	const Block blocks[] = {
		{ "TITLE_TILES pattern table", 0x9024 },
		{ "IN_GAME_TILES pattern table", 0x7D84 }
	};

	for (const Block& block : blocks) {
		memset(machine.Vdp().VramWritable(), 0, 0x4000);
		for (int third = 0; third < 3; third++) {
			if (!CallOriginal(machine, 0x4220, block.source,
					static_cast<uint16_t>(third * 2048))) {
				printf("  %s: the original did not return\n", block.name);
				failed++;
			}
		}

		Screen screen;
		memset(screen.VramPointer(), 0, 0x4000);
		VdpBlocks::WritePatternTable(kRomImage + (block.source - 0x4000),
			screen);

		failed += Compare(block.name, screen, machine, 0x0000, 0x1800);
	}

	// The colour table: FILL_COLORS_ALL_SCREEN takes no arguments.
	memset(machine.Vdp().VramWritable(), 0, 0x4000);
	if (!CallOriginal(machine, 0x41FF, 0, 0)) {
		printf("  the colour fill did not return\n");
		failed++;
	}
	Screen screen;
	memset(screen.VramPointer(), 0, 0x4000);
	VdpBlocks::FillColorsAllScreen(kRomImage + (0x93F4 - 0x4000), screen);
	failed += Compare("FILL_COLORS_ALL_SCREEN", screen, machine, 0x2000, 0x3800);

	printf("%s\n", failed == 0 ? "the title's graphics arrive as they do on the cartridge"
		: "the title's graphics differ");
	return failed == 0 ? 0 : 1;
}
