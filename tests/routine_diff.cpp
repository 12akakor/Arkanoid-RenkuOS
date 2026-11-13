/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of the ported ball routines against the original.
//
//   ./routine_diff build/arkanoid.rom [frames]
//
// The original is the oracle: the cartridge is run to a real game state, then
// each routine is called on the reference machine and in the port, from the
// same memory, and the game variables are compared byte for byte.
//
// Each routine is called for all three balls and for a set of states chosen
// to reach every branch (positive and negative skewness, both signs of speed,
// the 40th bounce, a sticky and an enlarged Vaus). The same state is given to
// both sides, so a disagreement is the port's.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <functional>
#include <string>
#include <vector>

#include "game/Ball.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kCompareFrom = 0xE000;
const uint16_t kCompareTo = 0xE600;

const uint16_t kBallTables = GameRam::kBallTables;
const uint16_t kBallSprites = GameRam::kBallSprites;


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


uint16_t
Table(int ball)
{
	return kBallTables + static_cast<uint16_t>(GameRam::kBallTableLength * ball);
}


uint16_t
Sprite(int ball)
{
	return kBallSprites + static_cast<uint16_t>(GameRam::kSpriteLength * ball);
}


// Calls one routine of the original with iy = its ball table and ix = its
// sprite, stopping when it returns. Interrupts are off, so only the routine
// runs.
bool
CallOriginal(Machine1& machine, uint16_t address, int ball)
{
	Z80Regs& r = machine.Cpu().r;
	// Stepping has to start from the routine, not from the game's HALT.
	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.ix = Sprite(ball);
	r.iy = Table(ball);
	r.af = 0;
	r.bc = 0;
	r.de = 0;
	r.hl = 0;
	r.sp = 0xF000;
	r.sp -= 2;
	machine.Write(r.sp, static_cast<uint8_t>(kSentinel & 0xff));
	machine.Write(r.sp + 1, static_cast<uint8_t>(kSentinel >> 8));
	r.pc = address;

	for (int step = 0; step < 200000; step++) {
		machine.StepInstruction();
		if (r.pc == kSentinel)
			return true;
	}
	return false;
}


enum Routine {
	kUpdateSpeed,
	kCheckBounces,
	kVerticalBounce,
	kHorizontalBounce,
	kInvertSkewness,
	kGlueAndSkewness,
	kOutBelow
};


struct Test {
	const char*		name;
	uint16_t		address;
	Routine			routine;
	bool			perBall;
};


void
RunPort(State& state, Routine routine, int ball)
{
	Ball balls(state);
	switch (routine) {
		case kUpdateSpeed:
			balls.UpdateSpeed(ball);
			break;
		case kCheckBounces:
			balls.CheckBouncesAndChangeSkewness();
			break;
		case kVerticalBounce:
			balls.VerticalBounce(ball);
			break;
		case kHorizontalBounce:
			balls.HorizontalBounce(ball);
			break;
		case kInvertSkewness:
			balls.InvertVerticalSkewness(ball);
			break;
		case kGlueAndSkewness:
			balls.CheckGlueAndSkewness(ball, ball);
			break;
		case kOutBelow:
			balls.OutBelow(ball, ball);
			break;
	}
}


struct Variant {
	const char*		name;
	std::function<void(std::vector<uint8_t>&, int)> setup;
};


void
Put(std::vector<uint8_t>& ram, uint16_t address, uint8_t value)
{
	ram[address] = value;
}


}	// namespace


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: routine_diff <rom> [frames]\n");
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

	// A real game: START at the title, then the ball launched.
	long frames = argc > 2 ? atol(argv[2]) : 1000;
	machine.Reset();
	for (long frame = 0; frame < frames; frame++) {
		for (int row = 0; row < 11; row++) {
			for (int bit = 0; bit < 8; bit++)
				machine.SetKey(row, bit, false);
		}
		if ((frame >= 320 && frame < 326) || (frame >= 900 && frame < 906))
			machine.SetKey(8, 0, true);
		machine.RunFrame();
	}

	std::vector<uint8_t> captured(0x10000);
	memcpy(captured.data(), machine.Ram(), captured.size());

	printf("%ld frames: level %d, ball 0 active %d, skewness %d, Y speed %d,"
		" X speed %d, speed position %d\n", frames,
		captured[GameRam::kLevel], captured[Table(0) + GameRam::kActive],
		static_cast<int8_t>(captured[Table(0) + GameRam::kSkewness]),
		static_cast<int8_t>(captured[Table(0) + GameRam::kYSpeed]),
		static_cast<int8_t>(captured[Table(0) + GameRam::kXSpeed]),
		captured[Table(0) + GameRam::kSpeedPosition]);

	const Variant variants[] = {
		{ "captured", [](std::vector<uint8_t>&, int) {} },
		{ "right, up", [](std::vector<uint8_t>& ram, int ball) {
			Put(ram, Table(ball) + GameRam::kSkewness, 3);
			Put(ram, Table(ball) + GameRam::kYSpeed, 0xFE);
			Put(ram, Table(ball) + GameRam::kXSpeed, 2);
		} },
		{ "left, down", [](std::vector<uint8_t>& ram, int ball) {
			Put(ram, Table(ball) + GameRam::kSkewness, 0xFD);
			Put(ram, Table(ball) + GameRam::kYSpeed, 2);
			Put(ram, Table(ball) + GameRam::kXSpeed, 0xFE);
		} },
		{ "steep right, down", [](std::vector<uint8_t>& ram, int ball) {
			Put(ram, Table(ball) + GameRam::kSkewness, 6);
			Put(ram, Table(ball) + GameRam::kYSpeed, 2);
			Put(ram, Table(ball) + GameRam::kXSpeed, 3);
		} },
		{ "steep left, up", [](std::vector<uint8_t>& ram, int ball) {
			Put(ram, Table(ball) + GameRam::kSkewness, 0xFA);
			Put(ram, Table(ball) + GameRam::kYSpeed, 0xFE);
			Put(ram, Table(ball) + GameRam::kXSpeed, 3);
		} },
		{ "39 bounces", [](std::vector<uint8_t>& ram, int ball) {
			Put(ram, Table(ball) + GameRam::kActive, 1);
			Put(ram, GameRam::kBallBouncesCounter, 39);
		} },
		{ "speeding up", [](std::vector<uint8_t>& ram, int ball) {
			Put(ram, Table(ball) + GameRam::kActive, 1);
			Put(ram, Table(ball) + GameRam::kSpeedPosition, 10);
			Put(ram, Table(ball) + GameRam::kSpeedCounter, 15);
		} },
		{ "on Vaus", [](std::vector<uint8_t>& ram, int ball) {
			Put(ram, Table(ball) + GameRam::kActive, 1);
			Put(ram, Table(ball) + GameRam::kYSpeed, 2);
			Put(ram, GameRam::kVausX, 0x50);
			Put(ram, Sprite(ball) + GameRam::kSpriteY, 169);
			Put(ram, Sprite(ball) + GameRam::kSpriteX, 0x50 + 20);
		} },
		{ "on sticky Vaus", [](std::vector<uint8_t>& ram, int ball) {
			Put(ram, Table(ball) + GameRam::kActive, 1);
			Put(ram, Table(ball) + GameRam::kYSpeed, 2);
			Put(ram, GameRam::kGluingStatus, GameRam::kGluingStateSticky);
			Put(ram, GameRam::kVausX, 0x50);
			Put(ram, Sprite(ball) + GameRam::kSpriteY, 170);
			Put(ram, Sprite(ball) + GameRam::kSpriteX, 0x50 + 30);
		} },
		{ "on enlarged Vaus", [](std::vector<uint8_t>& ram, int ball) {
			Put(ram, Table(ball) + GameRam::kActive, 1);
			Put(ram, Table(ball) + GameRam::kYSpeed, 2);
			Put(ram, GameRam::kVausResizing, GameRam::kActionStateEnlarging);
			Put(ram, Table(ball) + GameRam::kGlue, 0);
			Put(ram, GameRam::kVausX, 0x40);
			Put(ram, Sprite(ball) + GameRam::kSpriteY, 168);
			Put(ram, Sprite(ball) + GameRam::kSpriteX, 0x40 + 45);
		} },
		{ "sounds queued", [](std::vector<uint8_t>& ram, int ball) {
			Put(ram, Table(ball) + GameRam::kActive, 1);
			Put(ram, Table(ball) + GameRam::kYSpeed, 2);
			Put(ram, GameRam::kSoundsCount, 7);
			Put(ram, GameRam::kVausX, 0x50);
			Put(ram, Sprite(ball) + GameRam::kSpriteY, 170);
			Put(ram, Sprite(ball) + GameRam::kSpriteX, 0x50 + 10);
		} }
	};

	const Test tests[] = {
		{ "UPDATE_BALL_SPEED", 0x9AF0, kUpdateSpeed, true },
		{ "CHECK_BALL_BOUNCES_AND_CHANGE_SKEWNESS", 0x99D1, kCheckBounces,
			false },
		{ "BALL_VERTICAL_BOUNCE", 0x9B5B, kVerticalBounce, true },
		{ "BALL_HORIZONTAL_BOUNCE", 0x9B80, kHorizontalBounce, true },
		{ "INVERT_BALL_VERTICAL_SKEWNESS", 0x9B8A, kInvertSkewness, true },
		{ "CHECK_UPDATE_BALL_GLUE_AND_SKEWNESS", 0x9BA8, kGlueAndSkewness,
			true },
		{ "BALL_OUT_BELOW", 0x9B2A, kOutBelow, true }
	};

	std::vector<uint8_t> expected(0x10000);
	int compared = 0;
	int failed = 0;

	for (const Test& test : tests) {
		for (const Variant& variant : variants) {
			int balls = test.perBall ? GameRam::kBallCount : 1;
			for (int ball = 0; ball < balls; ball++) {
				std::vector<uint8_t> initial = captured;
				variant.setup(initial, ball);

				// The original.
				memcpy(machine.Ram(), initial.data(), initial.size());
				if (!CallOriginal(machine, test.address, ball)) {
					printf("  %s: the original did not return (%s, ball %d)\n",
						test.name, variant.name, ball);
					failed++;
					continue;
				}
				memcpy(expected.data(), machine.Ram(), expected.size());

				// The port.
				State state;
				memcpy(state.Ram(), initial.data(), initial.size());
				RunPort(state, test.routine, ball);

				compared++;
				int different = 0;
				uint16_t first = 0;
				for (uint16_t address = kCompareFrom; address < kCompareTo;
						address++) {
					if (expected[address] == state.Ram()[address])
						continue;
					if (different == 0)
						first = address;
					different++;
				}
				if (different == 0)
					continue;

				failed++;
				printf("  %s, %s, ball %d: %d bytes differ, first at 0x%04x"
					" (original %02x, port %02x)\n", test.name, variant.name,
					ball, different, first, expected[first],
					state.Ram()[first]);
			}
		}
	}

	printf("%d of %d routine calls agree with the original\n", compared - failed,
		compared);
	return failed == 0 ? 0 : 1;
}
