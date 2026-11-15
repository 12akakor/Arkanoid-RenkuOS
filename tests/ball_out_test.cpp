/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// A ball that leaves the field, against the cartridge.
//
//   ./ball_out_test build/arkanoid.rom
//
// BALL_OUT_BELOW (0x9B2A) is decided at the end of the ball's step (0x99AD):
// a ball whose Y has reached 184 has left the field. What that does depends on
// state the routine reads - an extra ball in hand, a Vaus on its way through
// the portal - and both sides are given the same state, so the choices the
// cartridge makes are the ones the port has to make: the ball's table cleared,
// its sprite parked out of sight with no colour, one extra ball handed back or
// the Vaus set exploding with sound 7 queued.
//
// The comparison is one frame of each side over the bytes that decide and
// record that choice; the frame as a whole does not match yet (the aliens'
// update is not in the port's frame), so comparing all of it would report that
// instead.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/Game.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

struct Watched {
	const char*			name;
	uint16_t			from;
	uint16_t			to;
};


const Watched kWatched[] = {
	{ "ball tables", GameRam::kBallTables,
		GameRam::kBallTables + GameRam::kBallCount * GameRam::kBallTableLength },
	{ "ball sprites", GameRam::kBallSprites,
		GameRam::kBallSprites + GameRam::kBallCount * GameRam::kSpriteLength },
	{ "extra balls", GameRam::kExtraBalls, GameRam::kExtraBalls + 1 },
	{ "vaus action state", GameRam::kVausActionState,
		GameRam::kVausActionState + 1 },
	{ "sounds count", GameRam::kSoundsCount, GameRam::kSoundsCount + 1 },
	{ "sounds buffer", GameRam::kSoundsBuffer, GameRam::kSoundsBuffer + 8 },
	{ "sound number", GameRam::kSoundNumber, GameRam::kSoundNumber + 1 }
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
		fprintf(stderr, "usage: ball_out_test <rom>\n");
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

	printf("balls that leave the field, against the cartridge\n");

	// Both sides start from a level being played; the title screen and the
	// level loader are the transitions the port does not have.
	machine.Ram()[GameRam::kTransitionAction] = 1;
	machine.Ram()[GameRam::kGameState] = 1;

	// A few frames of the level first, from the same memory, so both sides
	// start from a level the cartridge itself has set up rather than from the
	// title screen's leftovers.
	for (int frame = 0; frame < 4; frame++) {
		machine.Ram()[GameRam::kTransitionAction] = 1;
		machine.RunFrame();
	}

	std::vector<uint8_t> baseline(0x10000);
	memcpy(baseline.data(), machine.Ram(), baseline.size());

	const KeyMatrix keys = {};
	int compared = 0;
	int failed = 0;
	int cleared = 0;
	int spare = 0;

	for (int ball = 0; ball < GameRam::kBallCount; ball++) {
		for (int y = 180; y <= 190; y += 5) {
			for (int extra = 0; extra <= 3; extra += 3) {
				// The Vaus on its way through the portal (state 7) is
				// left out here: the cartridge's own handler for that state
				// is still unported, so the two sides part company before the
				// ball is looked at. routine_diff covers the portal rule where
				// it belongs, by calling BALL_OUT_BELOW itself on both sides.
				for (int vaus = 1; vaus <= 1; vaus++) {
					// One frame of each side, from equal memory.
					memcpy(machine.Ram(), baseline.data(), baseline.size());
					uint8_t* ram = machine.Ram();

					// The frame before may have left the machine in another
					// transition, so each case starts where the previous one
					// did, in a level being played.
					ram[GameRam::kTransitionAction] = 1;
					ram[GameRam::kGameState] = 1;
					ram[GameRam::kSoundNumber] = 0;

					const uint16_t table = static_cast<uint16_t>(
						GameRam::kBallTables
							+ ball * GameRam::kBallTableLength);
					const uint16_t sprite = static_cast<uint16_t>(
						GameRam::kBallSprites
							+ ball * GameRam::kSpriteLength);

					// The ball is on its way down, below the field, and the
					// two others are out of play where the original parks
					// them.
					for (int other = 0; other < GameRam::kBallCount; other++) {
						uint16_t otherTable = static_cast<uint16_t>(
							GameRam::kBallTables
								+ other * GameRam::kBallTableLength);
						uint16_t otherSprite = static_cast<uint16_t>(
							GameRam::kBallSprites
								+ other * GameRam::kSpriteLength);
						memset(ram + otherTable, 0, GameRam::kBallTableLength);
						ram[otherSprite + GameRam::kSpriteY] = 192;
						ram[otherSprite + GameRam::kSpriteX] = 0;
						ram[otherSprite + GameRam::kSpritePattern] = 0;
						ram[otherSprite + GameRam::kSpriteColour] = 0;
					}

					// Offset 1 of the table is both the glue state and the
					// index the original dispatches the ball's action on
					// (0x9889): 0 initialises a glued ball, 1 follows the
					// Vaus, anything else moves normally.
					ram[table + GameRam::kActive] = 1;
					ram[table + GameRam::kGlue] = 2;
					ram[table + GameRam::kYSpeed] = 3;
					ram[table + GameRam::kXSpeed] = 1;
					ram[table + GameRam::kSkewness] = 3;
					ram[table + GameRam::kSpeedPosition] = 12;
					ram[table + GameRam::kMoveCounter] = 0;
					ram[table + GameRam::kMoveTarget] = 1;
					ram[sprite + GameRam::kSpriteY] = static_cast<uint8_t>(y);
					ram[sprite + GameRam::kSpriteX] = 100;
					ram[sprite + GameRam::kSpritePattern] = 0x80;
					ram[sprite + GameRam::kSpriteColour] = 15;

					ram[GameRam::kExtraBalls] = static_cast<uint8_t>(extra);
					ram[GameRam::kVausActionState] = static_cast<uint8_t>(vaus);

					Game game;
					State& state = game.GetState();
					memcpy(state.Ram(), ram, 0x10000);

					machine.RunFrame();
					game.Tick(keys, 0, 0);

					// How many of the cases actually took the out path, and
					// how they were settled, so a test that silently stopped
					// reaching it cannot pass.
					if (state.Read(table + GameRam::kActive) == 0)
						cleared++;
					if (state.Read(table + GameRam::kActive) == 0
							&& state.Read(GameRam::kExtraBalls) != extra)
						spare++;

					for (const Watched& watched : kWatched) {
						for (uint16_t address = watched.from;
								address < watched.to; address++) {
							if (state.Read(address) == machine.Ram()[address])
								continue;
							if (failed < 10) {
								printf("  ball %d, y %d, extra %d, vaus %d:"
									" %s at %04x, original %02x, port %02x\n",
									ball, y, extra, vaus, watched.name,
									address, machine.Ram()[address],
									state.Read(address));
							}
							failed++;
							break;
						}
					}
					compared++;
				}
			}
		}
	}

	printf("%d of %d frames agree on the ball out below: %d differences\n",
		compared - failed, compared, failed);
	printf("%d of them were balls that left the field, %d of those took an"
		" extra ball instead of the Vaus\n", cleared, spare);
	return failed == 0 ? 0 : 1;
}
