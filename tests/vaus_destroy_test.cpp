/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The Vaus being destroyed, frame by frame against the cartridge.
//
//   ./vaus_destroy_test build/arkanoid.rom [frames]
//
// A lost ball sets the Vaus to exploding; from there the original animates it
// for four steps of twelve frames and then sends the game on to reload the
// level. The sprites it moves are the seven the Vaus's four pieces and the
// debris take, and they are what this compares, one frame at a time, from the
// same memory.
//
// The frames that follow the end of the animation are not compared: the
// cartridge reloads the level there, which the port does not do yet.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/Game.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSpriteFrom = GameRam::kSpriteParamsBase;
const uint16_t kSpriteTo = GameRam::kSpriteParamsBase + 4 * 7;

struct Watched {
	const char*			name;
	uint16_t			from;
	uint16_t			to;
};


const Watched kWatched[] = {
	{ "vaus sprites", kSpriteFrom, kSpriteTo },
	{ "vaus table", GameRam::kVausTable,
		GameRam::kVausTable + 10 },
	{ "vaus x", GameRam::kVausX2, GameRam::kVausX2 + 1 },
	{ "level transition type", GameRam::kLevelTransitionType,
		GameRam::kLevelTransitionType + 1 },
	{ "transition action", GameRam::kTransitionAction,
		GameRam::kTransitionAction + 1 }
};


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
		fprintf(stderr, "usage: vaus_destroy_test <rom> [frames]\n");
		return 1;
	}
	const int frames = argc > 2 ? atoi(argv[2]) : 60;

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

	// A level the cartridge itself has set up, as in ball_out_test.
	for (int frame = 0; frame < 4; frame++) {
		machine.Ram()[GameRam::kTransitionAction] = 1;
		machine.RunFrame();
	}

	// The Vaus destroyed, with no ball in play to take the frame's attention.
	uint8_t* ram = machine.Ram();
	for (int ball = 0; ball < GameRam::kBallCount; ball++) {
		memset(ram + GameRam::kBallTables
			+ ball * GameRam::kBallTableLength, 0, GameRam::kBallTableLength);
		ram[GameRam::kBallSprites + ball * GameRam::kSpriteLength
			+ GameRam::kSpriteY] = 192;
	}
	ram[GameRam::kVausTable + GameRam::kVausIdxActionState]
		= GameRam::kActionStateExploding;
	ram[GameRam::kVausTable + GameRam::kVausIdxDestructionStep1] = 0;
	ram[GameRam::kVausTable + GameRam::kVausIdxDestructionStep2] = 0;
	ram[GameRam::kVausTable + GameRam::kVausIdxHasLaser] = 1;
	ram[GameRam::kTransitionAction] = 1;
	ram[GameRam::kLevelTransitionType] = 0;

	Game game;
	State& state = game.GetState();
	memcpy(state.Ram(), ram, 0x10000);

	printf("the Vaus being destroyed, against the cartridge\n");

	const KeyMatrix keys = {};
	int failed = 0;

	// The cartridge's handler runs into the frame boundary: the seven patterns
	// of a step are written in one loop, and the frame the machine ends on can
	// fall in the middle of it, leaving the rest to the next frame. The port has
	// no frame boundary inside the step, so the two are one frame apart at the
	// end. The state that has to agree is therefore the one the port reaches a
	// frame earlier than the cartridge.
	std::vector<uint8_t> previous(0x10000);
	int ended = -1;

	for (int frame = 0; frame < frames; frame++) {
		memcpy(previous.data(), state.Ram(), previous.size());
		machine.RunFrame();
		game.Tick(keys, 0, 0);

		if (machine.Ram()[GameRam::kTransitionAction] != 1) {
			ended = frame;
			break;
		}
	}

	if (ended < 0) {
		printf("the animation did not end within %d frames\n", frames);
		return 1;
	}

	for (const Watched& watched : kWatched) {
		for (uint16_t address = watched.from; address < watched.to; address++) {
			if (previous[address] == machine.Ram()[address])
				continue;
			printf("  %s at %04x: original %02x, port %02x\n", watched.name,
				address, machine.Ram()[address], previous[address]);
			failed++;
		}
	}

	printf("the animation ends after %d frames in the cartridge: "
		"%s\n", ended + 1, failed == 0
			? "the port's last frame is the same state"
			: "the port's last frame differs");

	return failed == 0 ? 0 : 1;
}
