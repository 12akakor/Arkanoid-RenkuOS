/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of the collision's hit-point computers.
//
//   ./hitpoint_test build/arkanoid.rom [cases]
//
// For a few hundred synthetic situations - ball position, speeds, skewness and
// brick row and column - the original's COMPUTE_PRECISE_HIT_POINT (0xA3D1) and
// COMPUTE_WALL_ADJACENT_HIT_POINT (0xA591) are called on the reference machine
// and the port's are run on the same memory. The whole game variable area and
// the carry flag have to agree.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/Collision.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kPreciseHitPoint = 0xA3D1;
const uint16_t kWallAdjacentHitPoint = 0xA591;

const uint16_t kComparedFrom = 0xE000;
const uint16_t kComparedTo = 0xE600;

uint16_t
Table(int ball)
{
	return GameRam::kBallTables
		+ static_cast<uint16_t>(GameRam::kBallTableLength * ball);
}

uint16_t
Sprite(int sprite)
{
	return GameRam::kBallSprites
		+ static_cast<uint16_t>(GameRam::kSpriteLength * sprite);
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


bool
CallOriginal(Machine1& machine, uint16_t routine, bool& carry)
{
	Z80Regs& r = machine.Cpu().r;
	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.af = 0;
	r.bc = 0;
	r.de = 0;
	r.hl = 0;
	r.ix = Sprite(0);
	r.iy = Table(0);
	r.sp = 0xF000;
	r.sp -= 2;
	machine.Write(r.sp, static_cast<uint8_t>(kSentinel & 0xff));
	machine.Write(r.sp + 1, static_cast<uint8_t>(kSentinel >> 8));
	r.pc = routine;

	for (int step = 0; step < 200000; step++) {
		machine.StepInstruction();
		if (r.pc == kSentinel) {
			carry = (r.f & Z80_CF) != 0;
			return true;
		}
	}
	return false;
}


// A small deterministic generator, so a failing case can be reproduced.
struct Random {
	uint32_t	state = 12345;

	uint32_t
	Next()
	{
		state = state * 1103515245u + 12345u;
		return (state >> 8) & 0xffffff;
	}

	int Range(int low, int high) { return low + Next() % (high - low + 1); }
};

}	// namespace


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: hitpoint_test <rom> [cases]\n");
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
	machine.Reset();
	for (int frame = 0; frame < 300; frame++)
		machine.RunFrame();

	std::vector<uint8_t> base(0x10000);
	memcpy(base.data(), machine.Ram(), base.size());

	const int cases = argc > 2 ? atoi(argv[2]) : 300;
	Random random;
	int compared = 0;
	int failed = 0;

	static const uint16_t routines[] = { kPreciseHitPoint,
		kWallAdjacentHitPoint, 0xA670, 0xA810, 0xA901 };
	static const char* names[] = { "COMPUTE_PRECISE_HIT_POINT",
		"COMPUTE_WALL_ADJACENT_HIT_POINT", "RESOLVE_CORNER_COLLISION",
		"HANDLE_CORNER_CASE_VERTICAL", "HANDLE_CORNER_CASE_HORIZONTAL" };
	const int routineCount = sizeof(routines) / sizeof(routines[0]);

	for (int which = 0; which < routineCount; which++) {
		for (int index = 0; index < cases; index++) {
			std::vector<uint8_t> initial = base;

			const int ballY = random.Range(0, 191);
			const int ballX = random.Range(0, 255);
			int ySpeed = random.Range(1, 3);
			int xSpeed = random.Range(1, 3);
			if (random.Range(0, 1) == 0)
				ySpeed = -ySpeed;
			if (random.Range(0, 1) == 0)
				xSpeed = -xSpeed;
			int skewness = random.Range(1, 7);
			if (random.Range(0, 1) == 0)
				skewness = -skewness;

			const uint16_t table = Table(0);
			const uint16_t sprite = Sprite(0);
			initial[sprite + GameRam::kSpriteY] = static_cast<uint8_t>(ballY);
			initial[sprite + GameRam::kSpriteX] = static_cast<uint8_t>(ballX);
			initial[table + GameRam::kYSpeed] = static_cast<uint8_t>(ySpeed);
			initial[table + GameRam::kXSpeed] = static_cast<uint8_t>(xSpeed);
			initial[table + GameRam::kSkewness]
				= static_cast<uint8_t>(skewness);
			initial[GameRam::kPrevYPixel]
				= static_cast<uint8_t>(ballY - ySpeed);
			initial[GameRam::kPrevXPixel]
				= static_cast<uint8_t>(ballX - xSpeed);
			initial[GameRam::kBrickRow]
				= static_cast<uint8_t>(random.Range(0, 11));
			initial[GameRam::kBrickCol]
				= static_cast<uint8_t>(random.Range(0, 10));

			memcpy(machine.Ram(), initial.data(), initial.size());
			bool carry = false;
			if (!CallOriginal(machine, routines[which], carry)) {
				printf("  %s: the original did not return (case %d)\n",
					names[which], index);
				failed++;
				continue;
			}
			std::vector<uint8_t> expected(0x10000);
			memcpy(expected.data(), machine.Ram(), expected.size());

			State state;
			memcpy(state.Ram(), initial.data(), initial.size());
			Collision collision(state);
			bool result = false;
			switch (which) {
				case 0:
					result = collision.ComputePreciseHitPoint(0);
					break;
				case 1:
					result = collision.ComputeWallAdjacentHitPoint(0);
					break;
				case 2:
					result = collision.ResolveCornerCollision(0);
					break;
				case 3:
					collision.HandleCornerCaseVertical(0, 0);
					break;
				default:
					collision.HandleCornerCaseHorizontal(0, 0);
					break;
			}

			compared++;
			// The last two return nothing; only the first three have a
			// carry flag to compare.
			if (which < 3 && result != carry) {
				if (failed < 10)
					printf("  %s case %d: carry original %d, port %d\n",
						names[which], index, carry, result);
				failed++;
				continue;
			}

			for (uint16_t address = kComparedFrom; address < kComparedTo;
					address++) {
				if (expected[address] == state.Ram()[address])
					continue;
				if (failed < 10)
					printf("  %s case %d: 0x%04x original %02x, port %02x"
						" (Y %d X %d ySpeed %d xSpeed %d skew %d row %d col %d,"
						" prevY %d prevX %d)\n", names[which], index, address,
						expected[address], state.Ram()[address], ballY, ballX,
						ySpeed, xSpeed, skewness,
						initial[GameRam::kBrickRow], initial[GameRam::kBrickCol],
						initial[GameRam::kPrevYPixel],
						initial[GameRam::kPrevXPixel]);
				failed++;
				break;
			}
		}
	}

	printf("%d of %d hit-point computations agree with the original\n",
		compared - failed, compared);
	return failed == 0 ? 0 : 1;
}
