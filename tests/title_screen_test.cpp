/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// DRAW_TITLE_SCREEN's drawing, checked against the cartridge.
//
//   ./title_screen_test build/arkanoid.rom
//
// The cartridge's routine is called on the reference machine with GAME_STATE
// and IN_DEMO at zero and the title's action not one the routine branches on, so
// it runs its drawing path; the port's TitleScreen::Draw runs beside it on the
// same scores. Every byte of the pattern table, the name table, the sprite
// attributes and the colour table has to match, and so do the score buffers and
// the memory the routine leaves behind.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/RomImage.h"
#include "game/Screen.h"
#include "game/State.h"
#include "game/TitleScreen.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kRamFilled = 0xE0C9;		// the 128 bytes CLEAR_SCREEN fills
const uint16_t kRamDecoded = 0xE58E;	// the decoded score buffers


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
CallOriginal(Machine1& machine)
{
	Z80Regs& r = machine.Cpu().r;
	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.hl = 0;
	r.de = 0;
	r.af = 0;
	r.bc = 0;
	r.sp = 0xF000 - 2;
	machine.Write(r.sp, static_cast<uint8_t>(kSentinel & 0xFF));
	machine.Write(r.sp + 1, static_cast<uint8_t>(kSentinel >> 8));
	r.pc = 0x4B8A;

	for (int step = 0; step < 20000000; step++) {
		machine.StepInstruction();
		if (r.pc == kSentinel)
			return true;
	}
	return false;
}


int
CompareVram(const char* name, Screen& screen, Machine1& machine, uint16_t from,
	uint16_t to)
{
	int different = 0;
	uint16_t first = 0;
	for (uint16_t address = from; address < to; address++) {
		if (screen.Vram(address) == machine.Vdp().Vram()[address])
			continue;
		if (different == 0)
			first = address;
		different++;
	}
	printf("  %-20s %04x-%04x: %d differ", name, from, to, different);
	if (different != 0)
		printf(" (first %04x)", first);
	printf("\n");
	return different == 0 ? 0 : 1;
}


int
CompareRam(const char* name, State& state, Machine1& machine, uint16_t from,
	uint16_t to)
{
	int different = 0;
	uint16_t first = 0;
	for (uint16_t address = from; address < to; address++) {
		if (state.Read(address) == machine.Read(address))
			continue;
		if (different == 0)
			first = address;
		different++;
	}
	printf("  %-20s %04x-%04x: %d differ", name, from, to, different);
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
		fprintf(stderr, "usage: title_screen_test <rom>\n");
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
	for (int frame = 0; frame < 600; frame++)
		machine.RunFrame();

	// The scores the drawing decodes, and the state that sends the routine
	// down its drawing path.
	const uint8_t scores[] = {
		0x80, 0x22, 0x00,	// SCORE_BCD, 2280 as it is stored
		0x01, 0x00, 0x00,	// ZEROS_BCD_BUFFER
		0x00, 0x50, 0x00	// HIGH_SCORE_BCD, 5000
	};
	const uint16_t scoreAt[] = { 0xE015, 0xE018, 0xE007 };

	State state;
	state.Clear();
	for (int i = 0; i < 3; i++) {
		for (int b = 0; b < 3; b++)
			machine.Write(static_cast<uint16_t>(scoreAt[i] + b),
				scores[i * 3 + b]),
			state.Write(static_cast<uint16_t>(scoreAt[i] + b),
				scores[i * 3 + b]);
	}

	machine.Write(0xE00B, 0);	// GAME_STATE
	machine.Write(0xE00D, 0);	// IN_DEMO
	machine.Write(0xE53C, 0);	// TITLE_SCREEN_ACTION, none of the three
	machine.Write(0xE544, 0);	// SCORE_POSITION

	state.Write(GameRam::kGameState, 0);
	state.Write(GameRam::kInDemo, 0);
	state.Write(GameRam::kBrickHitYPixel, 0);
	state.Write(GameRam::kScorePosition, 0);

	Screen screen;
	for (int i = 0; i < 8; i++)
		screen.SetRegister(i, machine.Vdp().Register(i));

	printf("DRAW_TITLE_SCREEN's drawing, against the cartridge\n");

	int failed = 0;
	if (!CallOriginal(machine)) {
		printf("  the original did not return\n");
		return 1;
	}
	TitleScreen::Draw(state, screen);

	failed += CompareVram("pattern table", screen, machine, 0x0000, 0x1800);
	failed += CompareVram("name table", screen, machine, 0x1800, 0x1B00);
	failed += CompareVram("sprite attributes", screen, machine, 0x1B00, 0x1B80);
	failed += CompareVram("colour table", screen, machine, 0x2000, 0x3800);
	failed += CompareRam("score buffers", state, machine, kRamDecoded,
		kRamDecoded + 0x12);
	failed += CompareRam("cleared memory", state, machine, kRamFilled,
		kRamFilled + 128);
	failed += CompareRam("sprite, action", state, machine, 0xE0CD, 0xE0D1);
	failed += CompareRam("title ticks", state, machine, 0xE53C, 0xE540);

	printf("%s\n", failed == 0 ? "the title screen is drawn as the cartridge draws it"
		: "the title screen differs from the cartridge's");
	return failed == 0 ? 0 : 1;
}
