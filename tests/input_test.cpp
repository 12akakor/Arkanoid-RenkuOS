/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of the ported input stage against the original.
//
//   ./input_test build/arkanoid.rom [frames]
//
// The original runs real frames on the reference machine with a scripted key
// matrix. Before each frame its memory is handed to the port, which runs its
// own input stage on the same keys and the same paddle values the original
// read from the PSG; then every byte the input stage owns is compared.
//
// Nothing else in the game writes those bytes, so a difference is the port's.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/Input.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

// The keys, one pattern per frame. These bits are the test's own choice of
// keys, not the port's layout: 0 up, 1 down, 2 left, 3 right, 4 SPACE,
// 5 GRAPH, 6 STOP.
const uint8_t kPatterns[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x08, 0x0c,
	0x20, 0x21, 0x22, 0x23, 0x30, 0x40, 0x60, 0x10, 0x13
};

// Everything the input stage writes and nothing else does.
const uint16_t kCompared[] = {
	GameRam::kControls,
	GameRam::kKeyboardInput,
	GameRam::kPaddleCount,
	GameRam::kPaddleCount + 1,
	GameRam::kPaddleStatus,
	GameRam::kPaddleStatus + 1,
	GameRam::kCheat1Activated,
	GameRam::kCheat1Counter,
	GameRam::kCheat2Activated,
	GameRam::kCheat2Counter,
	GameRam::kUseVausPaddle
};
const int kComparedCount = sizeof(kCompared) / sizeof(kCompared[0]);


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


// The input stage runs inside the frame's interrupt, and the game itself can
// change memory between the start of a frame and that interrupt (it clears
// its variables when a level starts). So the state the handler is handed has
// to be taken at the handler's own entry, and its result at its return.
struct HandlerCapture {
	HandlerCapture()
		:
		machine(NULL),
		returnAddress(0),
		inside(false),
		done(false)
	{
	}

	void
	Arm(Machine1& newMachine)
	{
		machine = &newMachine;
		inside = false;
		done = false;
	}

	void
	Step()
	{
		if (machine == NULL || done)
			return;

		const Z80Regs& r = machine->Cpu().r;
		if (!inside) {
			if (r.pc != kHandler)
				return;
			before.assign(machine->Ram(), machine->Ram() + 0x10000);
			returnAddress = static_cast<uint16_t>(machine->Ram()[r.sp]
				| (machine->Ram()[r.sp + 1] << 8));
			inside = true;
			return;
		}

		if (r.pc == returnAddress) {
			after.assign(machine->Ram(), machine->Ram() + 0x10000);
			done = true;
		}
	}

	static const uint16_t kHandler = 0x424A;

	Machine1*				machine;
	uint16_t				returnAddress;
	bool					inside;
	bool					done;
	std::vector<uint8_t>	before;
	std::vector<uint8_t>	after;
};


void
ApplyKeys(uint8_t pattern, KeyMatrix& keys, Machine1& machine)
{
	struct Mapping {
		uint8_t	bit;
		int		row;
		int		matrixBit;
	};
	const Mapping mapping[] = {
		{ 0x01, 8, 5 },	// up
		{ 0x02, 8, 6 },	// down
		{ 0x04, 8, 4 },	// left
		{ 0x08, 8, 7 },	// right
		{ 0x10, 8, 0 },	// SPACE
		{ 0x20, 6, 2 },	// GRAPH
		{ 0x40, 7, 4 }	// STOP
	};

	keys.Clear();
	for (int row = 0; row < 11; row++) {
		for (int bit = 0; bit < 8; bit++)
			machine.SetKey(row, bit, false);
	}
	for (const Mapping& key : mapping) {
		if ((pattern & key.bit) == 0)
			continue;
		keys.Set(key.row, key.matrixBit, true);
		machine.SetKey(key.row, key.matrixBit, true);
	}
}


}	// namespace


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: input_test <rom> [frames]\n");
		return 1;
	}

	std::vector<uint8_t> bios;
	std::vector<uint8_t> logo;
	std::vector<uint8_t> rom;
	if (!ReadFile("third_party/cbios/cbios_main_msx1.rom", bios)
		|| !ReadFile(argv[1], rom)) {
		fprintf(stderr, "cannot read the C-BIOS or the ROM\n");
		return 1;
	}
	ReadFile("third_party/cbios/cbios_logo_msx1.rom", logo);

	Machine1 machine;
	if (!machine.LoadBios(bios, logo) || !machine.LoadCartridge(rom)) {
		fprintf(stderr, "bad C-BIOS or ROM size\n");
		return 1;
	}

	// A real title screen to start from.
	machine.Reset();
	for (int frame = 0; frame < 300; frame++)
		machine.RunFrame();

	const int frames = argc > 2 ? atoi(argv[2]) : 400;
	HandlerCapture capture;
	machine.onStep = [&capture]() { capture.Step(); };

	int compared = 0;
	int failed = 0;
	int starts = 0;
	int playFrames = 0;
	int noHandler = 0;

	for (int frame = 0; frame < frames; frame++) {
		KeyMatrix keys;
		const uint8_t pattern
			= kPatterns[frame % (sizeof(kPatterns) / sizeof(kPatterns[0]))];
		ApplyKeys(pattern, keys, machine);

		capture.Arm(machine);
		// The game starts between frames, not inside the input handler, so
		// the transition is looked for across the frame as a whole.
		const uint8_t stateBefore = machine.Ram()[GameRam::kGameState];
		machine.RunFrame();
		if (stateBefore == 0
			&& machine.Ram()[GameRam::kGameState] != 0)
			starts++;
		if (!capture.done) {
			noHandler++;
			continue;
		}

		const std::vector<uint8_t>& before = capture.before;
		const std::vector<uint8_t>& after = capture.after;

		if (before[GameRam::kGameState] == 0
			&& after[GameRam::kGameState] != 0)
			starts++;
		if (after[GameRam::kGameState] != 0)
			playFrames++;

		// What the platform gives the port: the paddle values the
		// original read from the PSG on this frame.
		const uint16_t paddleCount = static_cast<uint16_t>(after[
			GameRam::kPaddleCount] | ((after[GameRam::kPaddleCount + 1]
			& 0x01) << 8));
		const uint8_t paddleStatus = after[GameRam::kPaddleStatus];

		State state;
		memcpy(state.Ram(), before.data(), before.size());
		Input input(state);
		input.Update(keys, paddleCount, paddleStatus);

		for (int i = 0; i < kComparedCount; i++) {
			const uint16_t address = kCompared[i];
			compared++;
			if (after[address] == state.Ram()[address])
				continue;
			if (failed < 20)
				printf("  frame %d (keys %02x), 0x%04x: original %02x,"
					" port %02x\n", frame, pattern, address, after[address],
					state.Ram()[address]);
			failed++;
		}
	}

	printf("%d of %d bytes agree with the original over %d frames"
		" (%d games started, %d frames in play, %d without a handled"
		" interrupt)\n", compared - failed, compared, frames, starts, playFrames,
		noHandler);
	return failed == 0 ? 0 : 1;
}
