/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Next level, same level, a life lost, frame by frame against the cartridge.
//
//   ./life_lost_test build/arkanoid.rom [frames]
//
// NEXT_OR_SAME_LEVEL (0x7B94) is what a destroyed Vaus leads to, with the
// transition type set to the same level: forty-eight frames pass inside the
// handler before the life goes - a `halt` loop there - and sixty more before
// the game runs again. The port counts those frames instead of halting, so what
// has to agree is which frame each change happens on, and the state after it.
//
// Three cases: lives in hand, the last life (which is GAME OVER), and the
// transition that carries the game on to the next level.
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
const uint16_t kAlienDoorTicks = 0xE515;
// The PSG driver's own variables, from SOUND_REG_MASK up to the end of its
// streams' state.
const uint16_t kSoundStateFrom = 0xE5C3;
const uint16_t kSoundStateTo = 0xE600;


// The aliens' own tick counters belong to their update, which the port's frame
// does not run yet, so they are not what this compares.
bool
Skipped(uint16_t address)
{
	if (address == kAlienDoorTicks)
		return true;
	// The PSG driver's per-tick state. The port keeps it in its player rather
	// than in the cartridge's variables, and what it does with it is compared
	// write by write in the sound tests.
	if (address >= kSoundStateFrom && address < kSoundStateTo)
		return true;
	for (int alien = 0; alien < GameRam::kBallCount; alien++) {
		const uint16_t ticks = static_cast<uint16_t>(GameRam::kAlienTable
			+ alien * GameRam::kAlienTableLength + 3);
		if (address == ticks)
			return true;
	}
	return false;
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


int
RunCase(const std::vector<uint8_t>& bios, const std::vector<uint8_t>& logo,
	const std::vector<uint8_t>& rom, const char* name, int lives,
	int transition, int frames)
{
	Machine1 machine;
	if (!machine.LoadBios(bios, logo) || !machine.LoadCartridge(rom)) {
		fprintf(stderr, "bad C-BIOS or ROM size\n");
		return 1;
	}
	machine.Reset();
	for (int frame = 0; frame < 300; frame++)
		machine.RunFrame();
	for (int frame = 0; frame < 4; frame++) {
		machine.Ram()[GameRam::kTransitionAction] = 1;
		machine.RunFrame();
	}

	// The frame boundary can fall inside the handler, and a snapshot taken there
	// is a state the cartridge is halfway through. Let it reach its `halt`
	// first, so both sides start from a state the cartridge has finished.
	for (int step = 0; step < 200000 && !machine.Cpu().r.halted; step++)
		machine.StepInstruction();

	// No balls in play, so nothing else moves while the transition runs.
	uint8_t* ram = machine.Ram();
	for (int ball = 0; ball < GameRam::kBallCount; ball++) {
		memset(ram + GameRam::kBallTables
			+ ball * GameRam::kBallTableLength, 0, GameRam::kBallTableLength);
		ram[GameRam::kBallSprites + ball * GameRam::kSpriteLength
			+ GameRam::kSpriteY] = 192;
	}
	ram[GameRam::kGameState] = 1;
	ram[GameRam::kLives] = static_cast<uint8_t>(lives);
	ram[GameRam::kLevelTransitionType] = static_cast<uint8_t>(transition);
	ram[GameRam::kTransitionAction] = 2;

	Game game;
	State& state = game.GetState();
	memcpy(state.Ram(), ram, 0x10000);

	printf("  %s\n", name);

	const KeyMatrix keys = {};
	int failed = 0;

	for (int frame = 0; frame < frames; frame++) {
		machine.RunFrame();

		// What the machine's own input hook read off the paddle port is what a
		// platform hands the port.
		const uint16_t count = static_cast<uint16_t>(
			machine.Ram()[GameRam::kPaddleCount]
			| ((machine.Ram()[GameRam::kPaddleCount + 1] & 1) << 8));
		const uint8_t status = machine.Ram()[GameRam::kPaddleStatus];
		game.Tick(keys, count, status);

		for (uint16_t address = kComparedFrom; address < kComparedTo;
				address++) {
			if (Skipped(address))
				continue;
			if (state.Read(address) == machine.Ram()[address])
				continue;
			if (failed < 6) {
				printf("    frame %d: %04x original %02x, port %02x\n", frame,
					address, machine.Ram()[address], state.Read(address));
			}
			failed++;
			break;
		}
	}

	printf("    %d frames, %d differ; lives %d, level %d, transition %d\n",
		frames, failed, machine.Ram()[GameRam::kLives],
		machine.Ram()[GameRam::kLevel], machine.Ram()[GameRam::kTransitionAction]);
	return failed;
}

}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: life_lost_test <rom> [frames]\n");
		return 1;
	}
	const int frames = argc > 2 ? atoi(argv[2]) : 100;

	std::vector<uint8_t> bios;
	std::vector<uint8_t> logo;
	std::vector<uint8_t> rom;
	if (!ReadFile("third_party/cbios/cbios_main_msx1.rom", bios)
			|| !ReadFile(argv[1], rom)) {
		fprintf(stderr, "cannot read the ROMs\n");
		return 1;
	}
	ReadFile("third_party/cbios/cbios_logo_msx1.rom", logo);

	printf("NEXT_OR_SAME_LEVEL, against the cartridge\n");

	int failed = 0;
	failed += RunCase(bios, logo, rom, "a life lost with lives in hand", 3,
		GameRam::kLevelTransitionSame, frames);
	// The wait and the GAME OVER music, up to the frame the tune ends on. What
	// the cartridge does after it - 240 more frames, then the clear and the
	// brick map wiped - is ported but not compared: the tune's end is where the
	// cartridge's driver and the port's player stop agreeing on SOUND_NUMBER,
	// and after the clear the game is at the title screen, which the port does
	// not have.
	failed += RunCase(bios, logo, rom, "the last life, GAME OVER", 0,
		GameRam::kLevelTransitionSame, 140);
	// The next level's sixty-frame wait, and no further: after it the cartridge
	// draws the title screen's transition, which the port does not have.
	failed += RunCase(bios, logo, rom, "on to the next level", 3,
		GameRam::kLevelTransitionNext, 60);

	printf("%s\n", failed == 0 ? "every frame agrees" : "frames differ");
	return failed == 0 ? 0 : 1;
}
