/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of CHECK_BRICK_HIT_AND_BOUNCE_BALL.
//
//   ./brickmain_test build/arkanoid.rom [cases]
//
// The routine is called on the reference machine with the ball, its speeds and
// the brick field in random states, and run on the same memory by the port. The
// game's variables, the video memory (a hit erases a brick) and the sprite
// parameters have to agree.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/BrickCollision.h"
#include "game/Screen.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kRoutine = 0x9C2D;

const uint16_t kComparedFrom = 0xE000;
const uint16_t kComparedTo = 0xE600;


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
struct Generator {
	uint32_t	state = 778899001;

	uint32_t
	Next()
	{
		state = state * 1103515245u + 12345u;
		return (state >> 8) & 0xffffff;
	}

	int Range(int low, int high) { return low + Next() % (high - low + 1); }
};

}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: brickmain_test <rom> [cases]\n");
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

	const int cases = argc > 2 ? atoi(argv[2]) : 400;
	Generator generator;
	int compared = 0;
	int failed = 0;

	for (int index = 0; index < cases; index++) {
		std::vector<uint8_t> initial = base;

		// Every level but the last one has bricks to hit.
		initial[GameRam::kLevel] = static_cast<uint8_t>(generator.Range(0, 0x1f));

		// The ball's speeds, and a skewness so the tables have something to
		// say. The sprite is placed anywhere on the field.
		int ySpeed = generator.Range(1, 8);
		int xSpeed = generator.Range(1, 8);
		if (generator.Range(0, 1) == 0)
			ySpeed = -ySpeed;
		if (generator.Range(0, 1) == 0)
			xSpeed = -xSpeed;
		int skewness = generator.Range(1, 7);
		if (generator.Range(0, 1) == 0)
			skewness = -skewness;

		const uint16_t table = Table(0);
		const uint16_t sprite = Sprite(0);
		const int ballY = generator.Range(0, 191);
		const int ballX = generator.Range(0, 255);
		initial[table + GameRam::kActive] = 1;
		initial[table + GameRam::kYSpeed] = static_cast<uint8_t>(ySpeed);
		initial[table + GameRam::kXSpeed] = static_cast<uint8_t>(xSpeed);
		initial[table + GameRam::kSkewness] = static_cast<uint8_t>(skewness);
		initial[sprite + GameRam::kSpriteY] = static_cast<uint8_t>(ballY);
		initial[sprite + GameRam::kSpriteX] = static_cast<uint8_t>(ballX);
		initial[GameRam::kPrevYPixel]
			= static_cast<uint8_t>(ballY - ySpeed);
		initial[GameRam::kPrevXPixel]
			= static_cast<uint8_t>(ballX - xSpeed);

		// The cells the helpers are handed, and the ones the routine works
		// out for itself, all start somewhere plausible.
		initial[GameRam::kCurrBrickY]
			= static_cast<uint8_t>(generator.Range(0, 11));
		initial[GameRam::kCurrBrickX]
			= static_cast<uint8_t>(generator.Range(0, 10));
		initial[GameRam::kPrevBrickY]
			= static_cast<uint8_t>(generator.Range(0, 12));
		initial[GameRam::kPrevBrickX]
			= static_cast<uint8_t>(generator.Range(0, 10));
		initial[GameRam::kBrickRow]
			= static_cast<uint8_t>(generator.Range(0, 11));
		initial[GameRam::kBrickCol]
			= static_cast<uint8_t>(generator.Range(0, 10));

		// The hit point the corner code leaves behind.
		for (int i = 0; i < 6; i++)
			initial[GameRam::kBrickHitYPixel + i]
				= static_cast<uint8_t>(generator.Range(0, 255));

		// The brick mask, so that bricks are sometimes there and sometimes
		// not, and the counters a hit touches.
		for (int i = 0; i < 17; i++)
			initial[0xE027 + i] = static_cast<uint8_t>(generator.Next());
		for (int i = 0; i < 0x40; i++)
			initial[0xE20D + i]
				= static_cast<uint8_t>(generator.Range(0, 3));
		initial[GameRam::kBricksLeft]
			= static_cast<uint8_t>(generator.Range(1, 0x20));
		initial[GameRam::kHardBricksRemainingHits]
			= static_cast<uint8_t>(generator.Range(0, 4));
		initial[0xE5A0] = 0;
		// No capsule: its type is the one thing that comes from the machine's
		// own random source, and this test is about the collision.
		initial[GameRam::kCapsulesLeft] = 0;
		initial[GameRam::kCapsuleIsFalling] = 0;
		initial[GameRam::kGameState]
			= static_cast<uint8_t>(generator.Range(0, 3));

		for (int i = 0; i < 22 * 12; i++) {
			initial[GameRam::kBackgroundTilemap + i]
				= static_cast<uint8_t>(generator.Range(0, 2) == 0
					? 0x23 + 2 * generator.Range(0, 4)
					: generator.Next());
		}

		uint8_t* vram = machine.Vdp().VramWritable();
		for (int i = 0; i < 0x4000; i++)
			vram[i] = static_cast<uint8_t>(generator.Next());

		memcpy(machine.Ram(), initial.data(), initial.size());
		bool carry = false;
		if (!CallOriginal(machine, kRoutine, carry)) {
			printf("  case %d: the original did not return\n", index);
			failed++;
			continue;
		}
		std::vector<uint8_t> expected(0x10000);
		memcpy(expected.data(), machine.Ram(), expected.size());
		std::vector<uint8_t> expectedVram(0x4000);
		memcpy(expectedVram.data(), machine.Vdp().Vram(),
			expectedVram.size());

		State state;
		memcpy(state.Ram(), initial.data(), initial.size());
		Screen screen;
		for (int reg = 0; reg < Screen::kRegisters; reg++)
			screen.SetRegister(reg, machine.Vdp().Register(reg));
		memcpy(screen.VramPointer(), expectedVram.data(),
			Screen::kVramSize);
		BrickCollision collision(state, screen);
		collision.CheckBrickHitAndBounceBall(0);

		compared++;

		bool differs = false;
		for (uint16_t address = kComparedFrom; address < kComparedTo;
				address++) {
			// The capsule type comes from the machine's own random source
			// (ld a,r), which the port deliberately does not copy.
			// The capsule's life counter and the sprite it is drawn with
			// come from that same number, down to the colour.
			if (address >= GameRam::kCapsulesRandomNum
					&& address <= GameRam::kCapsuleLivesCounterH)
				continue;
			if (address >= GameRam::kFallingCapsuleSprite
					&& address < GameRam::kFallingCapsuleSprite + 4)
				continue;
			if (address == GameRam::kCapsuleType)
				continue;
			if (expected[address] == state.Ram()[address])
				continue;
			if (failed < 10)
				printf("  case %d: 0x%04x original %02x, port %02x"
					" (sprite %d,%d speed %d,%d curr %d,%d prev %d,%d)\n",
					index, address, expected[address],
					state.Ram()[address], ballY, ballX, ySpeed, xSpeed,
					initial[GameRam::kCurrBrickY],
					initial[GameRam::kCurrBrickX],
					initial[GameRam::kPrevBrickY],
					initial[GameRam::kPrevBrickX]);
			if (failed < 10)
				printf("    cells orig %d,%d,%d,%d row/col %d,%d | port %d,%d,%d,%d"
					" row/col %d,%d\n",
					expected[0xE58A], expected[0xE58B], expected[0xE58C],
					expected[0xE58D], expected[0xE2AA], expected[0xE2AB],
					state.Ram()[0xE58A], state.Ram()[0xE58B],
					state.Ram()[0xE58C], state.Ram()[0xE58D],
					state.Ram()[0xE2AA], state.Ram()[0xE2AB]);
			failed++;
			differs = true;
			break;
		}
		if (differs)
			continue;

		for (uint16_t address = 0; address < Screen::kVramSize; address++) {
			if (expectedVram[address] == screen.Vram(address))
				continue;
			if (failed < 10)
				printf("  case %d: vram 0x%04x original %02x, port %02x\n",
					index, address, expectedVram[address],
					screen.Vram(address));
			failed++;
			break;
		}
	}

	printf("%d of %d brick hit checks agree with the original\n",
		compared - failed, compared);
	return failed == 0 ? 0 : 1;
}
