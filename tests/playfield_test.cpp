/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of the playfield's brick erasing.
//
//   ./playfield_test build/arkanoid.rom [cases]
//
// ERASE_BRICK (0xAB8F) is called on the reference machine for every brick
// position and run on the same memory by the port. It writes two characters of
// background into the name table, so the whole 16 KB of video memory is
// compared as well as the game's variables.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/Brick.h"
#include "game/Playfield.h"
#include "game/Screen.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kEraseBrick = 0xAB8F;
const uint16_t kCheckAndRemoveBrick = 0xABD3;

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
	uint32_t	state = 246813579;

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
		fprintf(stderr, "usage: playfield_test <rom> [cases]\n");
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
	Random random;
	int compared = 0;
	int failed = 0;

	for (int which = 0; which < 2; which++) {
	for (int index = 0; index < cases; index++) {
		std::vector<uint8_t> initial = base;

		// ERASE_BRICK gets every brick position and then positions outside
		// the field: it is pure arithmetic, so out-of-range input is well
		// defined and the port must follow it. CHECK_AND_REMOVE_BRICK
		// jumps through its own table, so outside the field the original
		// executes whatever the table's neighbour happens to be - that is
		// not behaviour to port, and it is only asked for the real field.
		if (which == 1 && index >= Brick::kRows * Brick::kColumns)
			break;

		int row;
		int column;
		if (index < Brick::kRows * Brick::kColumns) {
			row = index / Brick::kColumns;
			column = index % Brick::kColumns;
		} else {
			row = random.Range(0, 255);
			column = random.Range(0, 255);
		}
		initial[GameRam::kBrickRow] = static_cast<uint8_t>(row);
		initial[GameRam::kBrickCol] = static_cast<uint8_t>(column);

		// A random name table, so a character the port fails to write cannot
		// pass for a match.
		uint8_t* vram = machine.Vdp().VramWritable();
		for (int i = 0; i < 0x4000; i++)
			vram[i] = static_cast<uint8_t>(random.Next());

		memcpy(machine.Ram(), initial.data(), initial.size());
		bool carry = false;
		if (!CallOriginal(machine, which == 0 ? kEraseBrick
				: kCheckAndRemoveBrick, carry)) {
			printf("  routine %d: the original did not return (row %d,"
				" column %d)\n", which, row, column);
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
		memcpy(screen.VramPointer(), expectedVram.data(), Screen::kVramSize);
		Playfield playfield(state, screen);
		playfield.EraseBrick();

		if (which == 1) {
			State port;
			memcpy(port.Ram(), initial.data(), initial.size());
			Brick brick(port);
			brick.Remove(static_cast<uint8_t>(row),
				static_cast<uint8_t>(column));

			bool wrong = false;
			for (uint16_t address = kComparedFrom; address < kComparedTo;
					address++) {
				if (expected[address] == port.Ram()[address])
					continue;
				if (failed < 10)
					printf("  CHECK_AND_REMOVE_BRICK row %d column %d:"
						" 0x%04x original %02x, port %02x\n", row, column,
						address, expected[address],
						port.Ram()[address]);
				failed++;
				wrong = true;
				break;
			}
			if (!wrong)
				compared++;
			continue;
		}

		compared++;
		bool differs = false;
		for (uint16_t address = kComparedFrom; address < kComparedTo;
				address++) {
			if (expected[address] == state.Ram()[address])
				continue;
			if (failed < 10)
				printf("  ERASE_BRICK row %d column %d: 0x%04x original %02x,"
					" port %02x\n", row, column, address,
					expected[address], state.Ram()[address]);
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
				printf("  ERASE_BRICK row %d column %d: vram 0x%04x original"
					" %02x, port %02x\n", row, column, address,
					expectedVram[address], screen.Vram(address));
			failed++;
			break;
		}
	}
	}

	printf("%d of %d playfield computations agree with the original\n",
		compared - failed, compared);
	return failed == 0 ? 0 : 1;
}
