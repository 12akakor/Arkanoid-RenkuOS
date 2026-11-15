/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The title screen's loop, against the cartridge.
//
//   ./title_loop_test build/arkanoid.rom
//
// Four things the screen does with itself: it counts ticks while no one presses
// anything, it goes to the demo after 480 of them, START puts "GAME START" on
// the screen with its music and a 256-frame wait, and the two exits set up the
// game the cartridge sets up. Each is run on the reference machine from the same
// memory as the port's, and the settled memory and screen compared.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/Screen.h"
#include "game/State.h"
#include "game/TitleScreen.h"
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


// One cartridge routine, stopping at the sentinel or at 'stopPc'.
bool
CallOriginal(Machine1& machine, uint16_t routine, uint16_t stopPc)
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
	r.pc = routine;

	for (int step = 0; step < 4000000; step++) {
		machine.StepInstruction();
		if (r.pc == kSentinel || (stopPc != 0 && r.pc == stopPc))
			return true;
	}
	return false;
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
	if (different != 0) {
		printf("    %s %04x-%04x: %d differ (first %04x: machine %02x port %02x)\n",
			name, from, to, different, first, machine.Read(first),
			state.Read(first));
	}
	return different == 0 ? 0 : 1;
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
	if (different != 0) {
		printf("    %s %04x-%04x: %d differ (first %04x: machine %02x port %02x)\n",
			name, from, to, different, first, machine.Vdp().Vram()[first],
			screen.Vram(first));
	}
	return different == 0 ? 0 : 1;
}


// Both sides start from the same memory: zeros, and the title's action.
void
Prepare(State& state, Machine1& machine, Screen& screen, uint8_t action)
{
	state.Clear();
	for (uint16_t address = 0xE000; address < 0xE600; address++)
		machine.Write(address, 0);

	state.Write(GameRam::kBrickHitYPixel, action);
	machine.Write(0xE53C, action);

	// The same VDP registers on both sides, so the table bases agree.
	for (int i = 0; i < 8; i++)
		screen.SetRegister(i, machine.Vdp().Register(i));
}

}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: title_loop_test <rom>\n");
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

	printf("the title screen's loop, against the cartridge\n");

	int failures = 0;
	State state;
	Screen screen;

	// 1. Waiting: three frames, no input, the ticks go up by three.
	printf("  waiting counts ticks\n");
	Prepare(state, machine, screen, 1);
	for (int i = 0; i < 3; i++)
		CallOriginal(machine, 0x4B8A, 0);
	for (int i = 0; i < 3; i++)
		TitleScreen::Tick(state, screen);
	failures += CompareRam("ticks", state, machine, 0xE53C, 0xE540);

	// 2. The 480th tick: the demo, and the screen cleared.
	printf("  480 ticks go to the demo\n");
	Prepare(state, machine, screen, 1);
	machine.Write(0xE53F, 0xDF);	// 479 ticks
	machine.Write(0xE540, 0x01);
	state.Write(GameRam::kTitleTicks, 0xDF);
	state.Write(static_cast<uint16_t>(GameRam::kTitleTicks + 1), 0x01);
	CallOriginal(machine, 0x4B8A, 0);
	TitleScreen::Tick(state, screen);
	failures += CompareRam("action, demo, ticks", state, machine, 0xE00D, 0xE00E);
	failures += CompareRam("action", state, machine, 0xE53C, 0xE540);
	failures += CompareVram("cleared screen", screen, machine, 0x1800, 0x1B80);

	// 3. Space: "GAME START", its music and the 256-frame wait.
	printf("  START shows GAME START\n");
	Prepare(state, machine, screen, 1);
	machine.Write(0xE0BF, 0x10);	// CONTROLS: the space bar
	state.Write(GameRam::kControls, 0x10);
	CallOriginal(machine, 0x4B8A, 0);
	TitleScreen::Result result = TitleScreen::Tick(state, screen);
	failures += CompareRam("action, sound", state, machine, 0xE53C, 0xE53D);
	failures += CompareRam("sound number", state, machine, 0xE5C0, 0xE5C1);
	failures += CompareVram("GAME START", screen, machine, 0x19A8, 0x19B9);
	if (!result.playSound || result.frames != 256) {
		printf("    the port did not ask for the sound and the 256 frames\n");
		failures++;
	}

	// 4. START_GAME: the variables the game starts from.
	printf("  START clears the game's memory\n");
	Prepare(state, machine, screen, 2);
	state.Write(GameRam::kLives, 7);	// something to be overwritten
	machine.Write(0xE01D, 7);
	state.Write(GameRam::kCheat1Activated, 0);
	CallOriginal(machine, 0x4CC6, 0x4D09);
	TitleScreen::Tick(state, screen);
	failures += CompareRam("game memory", state, machine, 0xE000, 0xE600);

	// 5. The demo's entry: the next demo level out of the four.
	printf("  the demo picks its next level\n");
	Prepare(state, machine, screen, 5);
	CallOriginal(machine, 0x4CA2, 0x4D09);
	TitleScreen::Tick(state, screen);
	failures += CompareRam("game, demo level, level", state, machine, 0xE000,
		0xE002);
	failures += CompareRam("level", state, machine, 0xE01B, 0xE01D);
	failures += CompareRam("transition", state, machine, 0xE022, 0xE023);

	// 6. The level reload's start, up to the cartridge's own 48-frame wait.
	printf("  the level reload's start\n");
	Prepare(state, machine, screen, 2);
	state.Write(GameRam::kGameState, 1);
	machine.Write(0xE00B, 1);
	state.Write(GameRam::kLevelDisplay, 3);
	machine.Write(0xE01C, 3);
	state.Write(GameRam::kBrickMap + 5, 0xAA);
	machine.Write(0xE02C, 0xAA);
	const bool reached = CallOriginal(machine, 0x4D09, 0x4D40);
	if (!reached) {
		printf("    the original did not reach the wait\n");
		failures++;
	} else {
		const bool wait = TitleScreen::StartLevel(state, screen);
		if (!wait) {
			printf("    the port did not ask for the wait\n");
			failures++;
		}
		failures += CompareRam("brick map", state, machine, 0xE027, 0xE032);
		failures += CompareVram("scores and round", screen, machine, 0x1800,
			0x1B80);
	}

	printf("%s\n", failures == 0
		? "the title loop runs as the cartridge runs it"
		: "the title loop differs from the cartridge's");
	return failures == 0 ? 0 : 1;
}
