/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of the aliens, Doh and the ball's movement.
//
//   ./motion_test build/arkanoid.rom [cases]
//
// Four routines, each called on the reference machine with the game in a random
// state and run on the same memory by the port:
//
//   CHECK_ANY_BALL_HITS_ALIEN (0x79A5)
//   CHECK_BALL_HITS_ALIEN     (0x79FD)
//   CHECK_DOH_HIT_AND_BOUNCE_BALL (0x967B)
//   UPDATE_BALL_POSITION      (0x99DF)
//
// The game's variables and the whole video memory have to agree. The four bytes
// that come from the machine's own random source are left out, as elsewhere.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/BallMotion.h"
#include "game/Screen.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;

const uint16_t kAnyBallHitsAlien = 0x79A5;
const uint16_t kBallHitsAlien = 0x79FD;
const uint16_t kDohHit = 0x967B;
const uint16_t kUpdatePosition = 0x99DF;

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
	uint32_t	state = 13371337;

	uint32_t
	Next()
	{
		state = state * 1103515245u + 12345u;
		return (state >> 8) & 0xffffff;
	}

	int Range(int low, int high) { return low + Next() % (high - low + 1); }
};


// The four bytes the capsule takes from `ld a,r`, on top of the three it keeps
// its life in.
bool
RandomCapsuleByte(uint16_t address)
{
	return (address >= GameRam::kCapsulesRandomNum
			&& address <= GameRam::kCapsuleLivesCounterH)
		|| (address >= GameRam::kFallingCapsuleSprite
			&& address < GameRam::kFallingCapsuleSprite + 4)
		|| address == GameRam::kCapsuleType;
}

}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: motion_test <rom> [cases]\n");
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
	const uint16_t routines[4] = { kAnyBallHitsAlien, kBallHitsAlien, kDohHit,
		kUpdatePosition };
	const char* names[4] = { "CHECK_ANY_BALL_HITS_ALIEN",
		"CHECK_BALL_HITS_ALIEN", "CHECK_DOH_HIT_AND_BOUNCE_BALL",
		"UPDATE_BALL_POSITION" };

	Generator generator;
	int compared = 0;
	int failed = 0;

	for (int which = 0; which < 4; which++) {
		for (int index = 0; index < cases; index++) {
			std::vector<uint8_t> initial = base;

			// Half the time the last level, so Doh is in play.
			initial[GameRam::kLevel] = generator.Range(0, 1) == 0
				? static_cast<uint8_t>(generator.Range(0, 0x1f))
				: static_cast<uint8_t>(GameRam::kFinalLevel);

			// The ball: a direction, a place in its speed table, and a
			// place on the field.
			int skewness = generator.Range(1, 7);
			if (generator.Range(0, 1) == 0)
				skewness = -skewness;
			const uint8_t ballY = static_cast<uint8_t>(generator.Range(0, 255));
			const uint8_t ballX = static_cast<uint8_t>(generator.Range(0, 255));

			for (int ball = 0; ball < 3; ball++) {
				const uint16_t table = Table(ball);
				const uint16_t sprite = Sprite(ball);
				initial[table + GameRam::kActive]
					= static_cast<uint8_t>(generator.Range(0, 3) != 0);
				initial[table + GameRam::kYSpeed]
					= static_cast<uint8_t>(generator.Range(1, 8)
						* (generator.Range(0, 1) == 0 ? -1 : 1));
				initial[table + GameRam::kXSpeed]
					= static_cast<uint8_t>(generator.Range(1, 8)
						* (generator.Range(0, 1) == 0 ? -1 : 1));
				initial[table + GameRam::kSkewness]
					= static_cast<uint8_t>(ball == 0 ? skewness : -skewness);
				initial[table + GameRam::kSpeedPosition]
					= static_cast<uint8_t>(generator.Range(0, 15));
				initial[table + GameRam::kSpeedMultiplier]
					= static_cast<uint8_t>(generator.Range(0, 2));
				initial[table + GameRam::kMoveTarget]
					= static_cast<uint8_t>(generator.Range(1, 3));
				initial[table + GameRam::kMoveCounter]
					= static_cast<uint8_t>(generator.Range(0, 3));
				initial[sprite + GameRam::kSpriteY]
					= static_cast<uint8_t>(ball == 0 ? ballY : generator.Range(0, 255));
				initial[sprite + GameRam::kSpriteX]
					= static_cast<uint8_t>(ball == 0 ? ballX : generator.Range(0, 255));
				initial[sprite + GameRam::kSpritePattern]
					= static_cast<uint8_t>(generator.Range(0, 3));
				initial[sprite + GameRam::kSpriteColour]
					= static_cast<uint8_t>(generator.Range(0, 15));
			}

			// The aliens: their state and where they are drawn.
			for (int i = 0; i < 3 * GameRam::kAlienTableLength; i++) {
				initial[GameRam::kAlienTable + i]
					= static_cast<uint8_t>(generator.Range(0, 2));
			}
			for (int alien = 0; alien < 3; alien++) {
				const uint16_t params = static_cast<uint16_t>(
					GameRam::kAlienSpriteParams
						+ alien * GameRam::kSpriteParamsLength);
				initial[params + GameRam::kSpriteY]
					= static_cast<uint8_t>(generator.Range(0, 255));
				initial[params + GameRam::kSpriteX]
					= static_cast<uint8_t>(generator.Range(0, 255));
				initial[params + GameRam::kSpritePattern]
					= static_cast<uint8_t>(generator.Range(0, 3));
				initial[params + GameRam::kSpriteColour]
					= static_cast<uint8_t>(generator.Range(0, 2) == 0
						? 0 : generator.Range(1, 15));
			}

			// Doh's counters.
			initial[GameRam::kDohReceivedHits]
				= static_cast<uint8_t>(generator.Range(0, 13));
			initial[GameRam::kDohBeenHit]
				= static_cast<uint8_t>(generator.Range(0, 1));
			initial[GameRam::kTblDohHit] = 0;
			initial[GameRam::kDohTable] = 0;

			// The bricks, so a step has something to hit.
			for (int i = 0; i < 17; i++)
				initial[0xE027 + i] = static_cast<uint8_t>(generator.Next());
			initial[GameRam::kBricksLeft]
				= static_cast<uint8_t>(generator.Range(1, 0x20));
			initial[GameRam::kGameState]
				= static_cast<uint8_t>(generator.Range(0, 3));
			initial[GameRam::kCapsulesLeft] = 0;
			initial[GameRam::kCapsuleIsFalling] = 0;

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
			if (!CallOriginal(machine, routines[which], carry)) {
				printf("  %s case %d: the original did not return\n",
					names[which], index);
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

			BallMotion motion(state, screen);
			bool result = false;
			switch (which) {
				case 0:
					motion.AlienState().CheckAnyBallHitsAlien();
					break;
				case 1:
					result = motion.AlienState().CheckBallHitsAlien(0);
					// The original answers in the carry; there is
					// nothing else to read back for this one.
					break;
				case 2:
					motion.AlienState().CheckDohHitAndBounceBall(0);
					break;
				default:
					motion.UpdatePosition(0);
					break;
			}

			compared++;


			bool differs = false;
			for (uint16_t address = kComparedFrom; address < kComparedTo;
					address++) {
				if (RandomCapsuleByte(address))
					continue;
				if (expected[address] == state.Ram()[address])
					continue;
				if (failed < 10)
					printf("  %s case %d: 0x%04x original %02x, port %02x"
						" (sprite %d,%d skewness %d)\n", names[which], index,
						address, expected[address], state.Ram()[address],
						ballY, ballX, skewness);
				failed++;
				differs = true;
				break;
			}
			if (differs)
				continue;

			for (uint16_t address = 0; address < Screen::kVramSize;
					address++) {
				if (expectedVram[address] == screen.Vram(address))
					continue;
				if (failed < 10)
					printf("  %s case %d: vram 0x%04x original %02x,"
						" port %02x\n", names[which], index, address,
						expectedVram[address], screen.Vram(address));
				failed++;
				break;
			}
		}
	}

	printf("%d of %d alien, Doh and movement calls agree with the original\n",
		compared - failed, compared);
	return failed == 0 ? 0 : 1;
}
