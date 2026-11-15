/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// One frame of the game, against one frame of the cartridge.
//
//   ./frame_test build/arkanoid.rom [frames] [first difference to show]
//
// The reference machine is left to run its own VBlank handler and main loop,
// one frame at a time. The port runs its Game::Tick from the same memory, byte
// for byte, and after every frame the game variables have to agree. The first
// address that does not is the piece of the frame that is still missing, which
// is what this test is for while the handler is being finished.
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
const uint16_t kComparedTo = 0xE600;

const char*
Name(uint16_t address)
{
	switch (address) {
		case GameRam::kControls:		return "controls";
		case GameRam::kKeyboardInput:	return "keyboard input";
		case GameRam::kGameState:		return "game state";
		case GameRam::kTransitionAction: return "transition action";
		case GameRam::kSoundsCount:		return "sounds count";
		case GameRam::kSoundNumber:		return "sound number";
		case GameRam::kBrickRow:		return "brick row";
		case GameRam::kBrickCol:		return "brick col";
		case GameRam::kBricksLeft:		return "bricks left";
		case GameRam::kScorePosition:	return "score position";
		
		case GameRam::kVausX2:			return "vaus x";
	}
	return "";
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

}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: frame_test <rom> [frames]\n");
		return 1;
	}
	const int frames = argc > 2 ? atoi(argv[2]) : 120;

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

	printf("one frame of the port against one frame of the cartridge\n");

	// Both sides into a level being played. The title screen and the level
	// loader are the two transitions the port does not have yet, so a frame
	// from either of them would compare those instead of the frame that is
	// there: this puts both sides on the part that is ported.
	machine.Ram()[GameRam::kTransitionAction] = 1;
	machine.Ram()[GameRam::kGameState] = 1;

	Game game;
	State& state = game.GetState();
	memcpy(state.Ram(), machine.Ram(), 0x10000);

	const KeyMatrix keys = {};
	int differed = 0;
	int firstFrame = -1;
	uint16_t firstAddress = 0;

	for (int frame = 0; frame < frames; frame++) {
		machine.RunFrame();

		// What the machine's own input hook read off the ports this frame is
		// what a platform would hand the port: the paddle's nine bits and its
		// button state. Everything downstream of that is the comparison.
		const uint16_t count = static_cast<uint16_t>(
			machine.Ram()[GameRam::kPaddleCount]
			| ((machine.Ram()[GameRam::kPaddleCount + 1] & 1) << 8));
		const uint8_t status = machine.Ram()[GameRam::kPaddleStatus];

		game.Tick(keys, count, status);

		for (uint16_t address = kComparedFrom; address < kComparedTo;
				address++) {
			if (state.Read(address) == machine.Ram()[address])
				continue;
			if (differed == 0) {
				firstFrame = frame;
				firstAddress = address;
			}
			if (differed < 12) {
				printf("  frame %d: %04x %-18s original %02x, port %02x\n",
					frame, address, Name(address), machine.Ram()[address],
					state.Read(address));
				printf("    parameters 0xE0C9..0xE0DD\n");
				for (uint16_t a = 0xE0C9; a <= 0xE0DD; a += 4) {
					printf("      %04x  original %02x %02x %02x %02x"
						"   port %02x %02x %02x %02x\n", a,
						machine.Ram()[a], machine.Ram()[a + 1],
						machine.Ram()[a + 2], machine.Ram()[a + 3],
						state.Read(a), state.Read(a + 1),
						state.Read(a + 2), state.Read(a + 3));
				}
				printf("    shadow 0xE18D..0xE19C\n");
				for (uint16_t a = 0xE18D; a <= 0xE195; a += 4) {
					printf("      %04x  original %02x %02x %02x %02x"
						"   port %02x %02x %02x %02x\n", a,
						machine.Ram()[a], machine.Ram()[a + 1],
						machine.Ram()[a + 2], machine.Ram()[a + 3],
						state.Read(a), state.Read(a + 1),
						state.Read(a + 2), state.Read(a + 3));
				}
			}
			differed++;
			break;
		}
	}

	if (differed == 0)
		printf("  %d frames, every game variable the same\n", frames);
	else
		printf("  %d frames, the first difference at frame %d, address %04x"
			" (%s)\n", frames, firstFrame, firstAddress, Name(firstAddress));

	return differed == 0 ? 0 : 1;
}
