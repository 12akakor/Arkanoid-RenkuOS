/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Differential test of ADVANCE_SOUND_STREAM_IF_READY.
//
//   ./advance_test build/arkanoid.rom [cases]
//
// The stream is put in an arbitrary state, and the byte its pointer aims at is
// either zero (the counts run) or not (a command is waiting). The carry the
// routine returns decides what the tick does next, so it is compared as well as
// the memory.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/SoundPlayer.h"
#include "game/State.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xFEFE;
const uint16_t kAdvance = 0xB4B5;	// called twice from the ISR

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
CallOriginal(Machine1& machine, uint16_t stream, uint16_t pointer,
	bool& carry)
{
	Z80Regs& r = machine.Cpu().r;
	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.af = 0;
	r.hl = stream;
	r.bc = pointer;
	r.de = 0;
	r.ix = 0;
	r.iy = 0;
	r.sp = 0xF000;
	r.sp -= 2;
	machine.Write(r.sp, static_cast<uint8_t>(kSentinel & 0xff));
	machine.Write(r.sp + 1, static_cast<uint8_t>(kSentinel >> 8));
	r.pc = kAdvance;

	for (int step = 0; step < 500000; step++) {
		machine.StepInstruction();
		if (r.pc == kSentinel) {
			carry = (r.f & Z80_CF) != 0;
			return true;
		}
	}
	return false;
}


struct Generator {
	uint32_t	state = 424242;

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
		fprintf(stderr, "usage: advance_test <rom> [cases]\n");
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

		const uint16_t stream = (index & 1) == 0
			? SoundPlayer::kStream1 : SoundPlayer::kStream2;
		// A pointer into the cartridge's data, where the sequences live.
		const uint16_t pointer = static_cast<uint16_t>(
			0xb800 + generator.Range(0, 0x3ff));

		initial[stream + SoundPlayer::kActive]
			= static_cast<uint8_t>(generator.Range(0, 1));
		initial[stream + SoundPlayer::kTicks]
			= static_cast<uint8_t>(generator.Range(0, 3));
		initial[stream + SoundPlayer::kChainCount]
			= static_cast<uint8_t>(generator.Range(0, 3));
		initial[stream + SoundPlayer::kRepeatCount]
			= static_cast<uint8_t>(generator.Range(0, 3));
		initial[stream + SoundPlayer::kFollowSoundId]
			= static_cast<uint8_t>(generator.Range(0, 200));

		// The descriptor the stream points at, inside the real table.
		const uint16_t descriptor = static_cast<uint16_t>(
			0xb422 + generator.Range(0, 27) * 5 + 2);
		initial[stream + SoundPlayer::kDescriptorLow] = descriptor & 0xff;
		initial[stream + SoundPlayer::kDescriptorHigh] = descriptor >> 8;

		// Half the cases leave the aimed-at byte at zero, the way the stream
		// reloads; the other half put a command there.
		if ((generator.Range(0, 1)) == 0)
			initial[pointer] = 0;
		else
			initial[pointer] = static_cast<uint8_t>(generator.Range(1, 255));

		memcpy(machine.Ram(), initial.data(), initial.size());
		bool expectedCarry = false;
		if (!CallOriginal(machine, stream, pointer, expectedCarry)) {
			printf("  case %d: the original did not return\n", index);
			failed++;
			continue;
		}
		std::vector<uint8_t> expected(0x10000);
		memcpy(expected.data(), machine.Ram(), expected.size());

		State state;
		memcpy(state.Ram(), initial.data(), initial.size());
		SoundPlayer player(state);
		const bool carry = player.Advance(stream, pointer);

		compared++;

		if (carry != expectedCarry) {
			if (failed < 10)
				printf("  case %d: carry original %d, port %d\n", index,
					expectedCarry, carry);
			failed++;
			continue;
		}

		for (uint16_t address = kComparedFrom; address < kComparedTo;
				address++) {
			if (expected[address] == state.Ram()[address])
				continue;
			if (failed < 10)
				printf("  case %d: 0x%04x original %02x, port %02x"
					" (ticks %d, active %d)\n", index, address,
					expected[address], state.Ram()[address],
					initial[stream + SoundPlayer::kTicks],
					initial[stream + SoundPlayer::kActive]);
			failed++;
			break;
		}
	}

	printf("%d of %d stream advances agree with the original\n",
		compared - failed, compared);
	return failed == 0 ? 0 : 1;
}
