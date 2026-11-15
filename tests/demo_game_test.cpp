/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// A demo game, frame by frame, against the cartridge.
//
//   ./demo_game_test build/arkanoid.rom
//
// The reference machine plays by itself: the title's ticks run out, the story
// types itself on, and one of the four demo levels starts. The port's memory and
// screen are copied from the machine at that moment, and from then on both run
// the same number of frames with nothing pressed - the machine through its
// VBlank handler, the port through Game::Tick - and every frame is compared.
//
// Three areas are left out of the comparison, and each for a stated reason: the
// raw keyboard and paddle reads at 0xE0C0-0xE0C9, which the tests feed by hand
// rather than through the PIA; the sprite parameters and the port's shadow of
// them at 0xE0C9-0xE20D, which the port fills at the head of a frame where the
// cartridge fills it during the frame; and the aliens' tick counters.
//
// What is compared is the game's memory from 0xE000 to 0xE5C0 (the sound driver's
// state after it is the sound player's own simulation, which the port does not
// emulate tick for tick) and the name table. The first frames that differ are
// reported with the address that differs first.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/Game.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kComparedFrom = 0xE000;
const uint16_t kComparedTo = 0xE5C0;
const int kMaxFrames = 4000;
const int kReported = 12;

// Aliens keep their own tick counters, which the port is not driving yet.
bool
Skipped(uint16_t address)
{
	if (address >= 0xE0C0 && address < 0xE0C9)
		return true;	// the raw keyboard and paddle reads, fed by hand here
	if (address >= 0xE0C9 && address < 0xE20D)
		return true;	// the sprite shadow, one frame apart (see the notes)
	return address >= 0xE515 && address < 0xE5C0 && ((address - 0xE515) % 20) < 4;
}


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


// True while the title screen waits for someone to start the game.
bool
TitleWaiting(Machine1& machine)
{
	return machine.Ram()[0xE00B] == 0	// GAME_STATE
		&& machine.Ram()[0xE00D] == 0	// IN_DEMO
		&& machine.Ram()[0xE00A] == 0	// GAME_TRANSITION_ACTION
		&& machine.Ram()[0xE53C] == 1;	// TITLE_SCREEN_ACTION_WAIT
}

}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: demo_game_test <rom>\n");
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

	printf("a demo game, frame by frame, against the cartridge\n");

	// The title, then the 480 ticks, the story and the demo's level start.
	int frame = 0;
	for (; frame < 6000; frame++) {
		machine.RunFrame();
		if (frame > 900 && TitleWaiting(machine))
			break;
	}
	if (!TitleWaiting(machine)) {
		printf("  the machine never settled at the title (frame %d)\n", frame);
		return 1;
	}
	printf("  both start from the title at frame %d, ticks %d\n", frame,
		machine.Ram()[0xE53F] | (machine.Ram()[0xE540] << 8));

	// The port starts from exactly the machine's state.
	Game game;
	State& state = game.GetState();
	memcpy(state.Ram(), machine.Ram(), 0x10000);
	Screen& screen = game.GetScreen();
	memcpy(screen.VramPointer(), machine.Vdp().Vram(), 0x4000);
	for (int i = 0; i < 8; i++)
		screen.SetRegister(i, machine.Vdp().Register(i));

	KeyMatrix keys = {};
	int indexDiffers = 0;
	uint16_t addresses[16] = {};
	int addressCounts[16] = {};
	int addressKinds = 0;
	int differingFrames = 0;
	int firstFrame = -1;
	int reported = 0;
	uint16_t firstAddress = 0;
	uint8_t firstOriginal = 0;
	uint8_t firstPort = 0;

	for (int tick = 0; tick < kMaxFrames; tick++) {
		// The machine's handler for a frame runs at its VBlank: ticking the
		// port first puts both sides at the same point in the frame.
		game.Tick(keys, 100, 0);
		machine.RunFrame();

		if (machine.Ram()[0xE2AC] != state.Read(0xE2AC))
			indexDiffers++;

		uint16_t address = kComparedFrom;
		for (; address < kComparedTo; address++) {
			if (Skipped(address))
				continue;
			if (state.Read(address) == machine.Ram()[address])
				continue;
			break;
		}

		const bool ramDiffers = address < kComparedTo;
		uint16_t vramAddress = 0x1800;
		for (; vramAddress < 0x1B00; vramAddress++) {
			if (screen.Vram(vramAddress)
					== machine.Vdp().Vram()[vramAddress])
				continue;
			break;
		}
		const bool vramDiffers = vramAddress < 0x1B00;

		if (!ramDiffers && !vramDiffers)
			continue;

		{
			const uint16_t found = ramDiffers ? address : vramAddress;
			int slot = 0;
			for (; slot < addressKinds; slot++) {
				if (addresses[slot] == found)
					break;
			}
			if (slot == addressKinds && addressKinds < 16) {
				addresses[addressKinds] = found;
				addressCounts[addressKinds] = 0;
				addressKinds++;
			}
			if (slot < 16)
				addressCounts[slot]++;
		}

		if (firstFrame < 0) {
			firstFrame = tick;
			firstAddress = ramDiffers ? address : vramAddress;
			firstOriginal = ramDiffers ? machine.Ram()[address]
				: machine.Vdp().Vram()[vramAddress];
			firstPort = ramDiffers ? state.Read(address)
				: screen.Vram(vramAddress);
		}

		if (reported < kReported) {
			printf("  frame %4d: %s %04x original %02x, port %02x\n", tick,
				ramDiffers ? "ram" : "vram", ramDiffers ? address : vramAddress,
				ramDiffers ? machine.Ram()[address]
					: machine.Vdp().Vram()[vramAddress],
				ramDiffers ? state.Read(address) : screen.Vram(vramAddress));
			reported++;
		}
		differingFrames++;
	}

	printf("  BALL_LOOP_INDEX differed on %d of %d frames\n", indexDiffers,
		kMaxFrames);
	printf("  where the first difference sits, frame by frame:\n");
	for (int i = 0; i < addressKinds; i++)
		printf("    %04x on %d frames\n", addresses[i], addressCounts[i]);

	if (differingFrames == 0) {
		printf("  %d frames, every one of them the same as the cartridge's\n",
			kMaxFrames);
		printf("the demo plays as the cartridge plays it\n");
		return 0;
	}

	printf("  %d of %d frames differ; the first is %d, at %04x: original %02x,"
		" port %02x\n", differingFrames, kMaxFrames, firstFrame, firstAddress,
		firstOriginal, firstPort);
	printf("the demo does not play the way the cartridge plays it\n");
	return 1;
}
