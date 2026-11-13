/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of the brick lookup.
//
//   ./brick_test build/arkanoid.rom
//
// BRICK_EXISTS_AT_ROWCOL (0xADA8) is called on the reference machine for every
// cell of the brick field and for several bitmask patterns, and the port's
// Brick::ExistsAt has to return the same answer and set DOH_BEEN_HIT the same
// way, without touching the bitmask.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/Brick.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kRoutine = 0xADA8;

// The 17 bytes the brick grid is packed into.
const uint16_t kBitmask = 0xE027;
const int kBitmaskLength = 17;

const uint16_t kBrickRow = 0xE2AA;
const uint16_t kBrickCol = 0xE2AB;


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
CallOriginal(Machine1& machine, bool& carry, bool trace)
{
	Z80Regs& r = machine.Cpu().r;
	// The game is sitting in its HALT loop between frames; stepping has to
	// start from the routine, not from there.
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
	r.pc = kRoutine;

	uint16_t recent[12] = {};
	for (int step = 0; step < 20000; step++) {
		for (int i = 0; i < 11; i++)
			recent[i] = recent[i + 1];
		recent[11] = r.pc;
		machine.StepInstruction();
		if (r.pc == kSentinel) {
			carry = (r.f & Z80_CF) != 0;
			return true;
		}
	}
	if (trace) {
		printf("    pc %04x sp %04x iy %04x hl %04x",
			static_cast<unsigned>(r.pc), static_cast<unsigned>(r.sp),
			static_cast<unsigned>(r.iy), static_cast<unsigned>(r.hl));
		printf("\n    last pcs:");
		for (int i = 0; i < 12; i++)
			printf(" %04x", recent[i]);
		printf("\n");
	}
	return false;
}


struct Pattern {
	const char*	name;
	uint8_t		fill[kBitmaskLength];
};


}	// namespace


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: brick_test <rom>\n");
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

	Pattern patterns[5];
	patterns[0].name = "all set";
	memset(patterns[0].fill, 0xff, kBitmaskLength);
	patterns[1].name = "all clear";
	memset(patterns[1].fill, 0x00, kBitmaskLength);
	patterns[2].name = "alternating";
	for (int i = 0; i < kBitmaskLength; i++)
		patterns[2].fill[i] = (i & 1) ? 0x55 : 0xaa;
	patterns[3].name = "one bit each";
	for (int i = 0; i < kBitmaskLength; i++)
		patterns[3].fill[i] = static_cast<uint8_t>(1u << (i % 8));
	patterns[4].name = "ramp";
	for (int i = 0; i < kBitmaskLength; i++)
		patterns[4].fill[i] = static_cast<uint8_t>(i * 15);

	int compared = 0;
	int failed = 0;

	for (const Pattern& pattern : patterns) {
		// Rows past the field are asked for, because the original clamps
		// them. The last column is not: its pointer in the cartridge's
		// table is 0x0000, so the original reads the BIOS page there. The
		// game never asks for it; see the note in Brick.cpp.
		for (int row = 0; row < Brick::kRows + 1; row++) {
			for (int column = 0; column < Brick::kColumns; column++) {
				std::vector<uint8_t> initial = base;
				for (int i = 0; i < kBitmaskLength; i++)
					initial[kBitmask + i] = pattern.fill[i];
				initial[kBrickRow] = static_cast<uint8_t>(row);
				initial[kBrickCol] = static_cast<uint8_t>(column);

				memcpy(machine.Ram(), initial.data(), initial.size());
				bool carry = false;
				if (!CallOriginal(machine, carry, failed == 0)) {
					printf("  the original did not return (%s, %d/%d)\n",
						pattern.name, row, column);
					failed++;
					continue;
				}

				std::vector<uint8_t> expected(0x10000);
				memcpy(expected.data(), machine.Ram(), expected.size());

				State state;
				memcpy(state.Ram(), initial.data(), initial.size());
				Brick brick(state);
				const bool present = brick.ExistsAt(static_cast<uint8_t>(row),
					static_cast<uint8_t>(column));

				compared++;
				if (present != carry) {
					if (failed < 20)
						printf("  %s, row %d col %d: original %d, port %d\n",
							pattern.name, row, column, carry, present);
					failed++;
					continue;
				}
				if (expected[GameRam::kDohBeenHit]
						!= state.Ram()[GameRam::kDohBeenHit]) {
					if (failed < 20)
						printf("  %s, row %d col %d: DOH_BEEN_HIT original"
							" %02x, port %02x\n", pattern.name, row, column,
							expected[GameRam::kDohBeenHit],
							state.Ram()[GameRam::kDohBeenHit]);
					failed++;
					continue;
				}
				for (int i = 0; i < kBitmaskLength; i++) {
					if (expected[kBitmask + i] == state.Ram()[kBitmask + i])
						continue;
					if (failed < 20)
						printf("  %s, row %d col %d: bitmask byte %d"
							" original %02x, port %02x\n", pattern.name, row,
							column, i, expected[kBitmask + i],
							state.Ram()[kBitmask + i]);
					failed++;
					break;
				}
			}
		}
	}

	printf("%d of %d brick lookups agree with the original\n", compared - failed,
		compared);
	return failed == 0 ? 0 : 1;
}
