/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of the score display.
//
//   ./hud_test build/arkanoid.rom [cases]
//
// DECODE_BCD_TO_ASCII (0x5420) and REMOVE_HEADING_ZEROS (0x5431) are called on
// the reference machine with buffer and score addresses and run on the same
// memory by the port; DRAW_SCORE_NUMBERS (0x53B9) additionally writes the name
// table, so that case compares video memory too: the port's Screen against the
// VDP's 16 KB.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/Hud.h"
#include "game/Screen.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kDecodeBcd = 0x5420;
const uint16_t kRemoveHeadingZeros = 0x5431;
const uint16_t kDrawScoreNumbers = 0x53B9;

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


bool
CallOriginal(Machine1& machine, uint16_t routine, uint16_t hl, uint16_t de,
	bool& carry)
{
	Z80Regs& r = machine.Cpu().r;
	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.af = 0;
	r.bc = 0;
	r.hl = hl;
	r.de = de;
	r.ix = 0;
	r.iy = 0;
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
	uint32_t	state = 987654321;

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
		fprintf(stderr, "usage: hud_test <rom> [cases]\n");
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

	const int cases = argc > 2 ? atoi(argv[2]) : 200;
	Random random;
	int compared = 0;
	int failed = 0;

	static const uint16_t routines[] = { kDecodeBcd, kRemoveHeadingZeros,
		kDrawScoreNumbers };
	static const char* names[] = { "DECODE_BCD_TO_ASCII",
		"REMOVE_HEADING_ZEROS", "DRAW_SCORE_NUMBERS" };

	for (int which = 0; which < 3; which++) {
		for (int index = 0; index < cases; index++) {
			std::vector<uint8_t> initial = base;

			uint16_t buffer = 0;
			uint16_t bcd = 0;
			if (which == 0) {
				// Any buffer and any three packed bytes: values outside the
				// decimal range exercise the nibble split as well.
				buffer = static_cast<uint16_t>(random.Range(0xE000, 0xE5F0));
				bcd = static_cast<uint16_t>(random.Range(0xE000, 0xE100));
				for (int i = 0; i < 6; i++)
					initial[buffer + i] = static_cast<uint8_t>(random.Next());
				for (int i = 0; i < 3; i++)
					initial[bcd + i] = static_cast<uint8_t>(random.Next());
			} else if (which == 1) {
				buffer = static_cast<uint16_t>(random.Range(0xE000, 0xE5F0));
				for (int i = 0; i < 6; i++) {
					initial[buffer + i] = random.Range(0, 2) == 0
						? '0' : static_cast<uint8_t>(random.Next());
				}
			} else {
				for (int i = 0; i < 3; i++) {
					initial[GameRam::kScoreBcd + i]
						= static_cast<uint8_t>(random.Next());
					initial[GameRam::kHighScoreBcd + i]
						= static_cast<uint8_t>(random.Next());
					initial[GameRam::kZerosBcdBuffer + i]
						= static_cast<uint8_t>(random.Next());
				}
				initial[GameRam::kScorePosition]
					= static_cast<uint8_t>(random.Range(0, 1));
				// The whole name table differs before every case, so an
				// untouched byte cannot pass as a match.
				uint8_t* vram = machine.Vdp().VramWritable();
				for (int i = 0; i < 0x4000; i++)
					vram[i] = static_cast<uint8_t>(random.Next());
			}

			memcpy(machine.Ram(), initial.data(), initial.size());
			bool carry = false;
			if (!CallOriginal(machine, routines[which], buffer, bcd, carry)) {
				printf("  %s: the original did not return (case %d)\n",
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
			if (which == 2) {
				// The same registers and video memory the machine has, so the
				// name base and every untouched byte are the original's.
				for (int reg = 0; reg < Screen::kRegisters; reg++)
					screen.SetRegister(reg, machine.Vdp().Register(reg));
				memcpy(screen.VramPointer(), expectedVram.data(),
					Screen::kVramSize);
			}
			Hud hud(state, screen);
			switch (which) {
				case 0:
					Hud::DecodeBcdToAscii(state, buffer, bcd);
					break;
				case 1:
					Hud::RemoveHeadingZeros(state, buffer);
					break;
				default:
					hud.DrawScoreNumbers();
					break;
			}

			compared++;
			for (uint16_t address = kComparedFrom; address < kComparedTo;
					address++) {
				if (expected[address] == state.Ram()[address])
					continue;
				if (failed < 10)
					printf("  %s case %d: 0x%04x original %02x, port %02x"
						" (buffer 0x%04x, bcd 0x%04x)\n", names[which],
						index, address, expected[address],
						state.Ram()[address], buffer, bcd);
				failed++;
				break;
			}

			if (which != 2)
				continue;
			for (uint16_t address = 0; address < Screen::kVramSize;
					address++) {
				if (expectedVram[address] == screen.Vram(address))
					continue;
				if (failed < 10)
					printf("  %s case %d: vram 0x%04x original %02x,"
						" port %02x (position %d)\n", names[which], index,
						address, expectedVram[address],
						screen.Vram(address),
						initial[GameRam::kScorePosition]);
				failed++;
				break;
			}
		}
	}

	printf("%d of %d score display computations agree with the original\n",
		compared - failed, compared);
	return failed == 0 ? 0 : 1;
}
