/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Compares the port's own renderer with the reference machine, pixel for
// pixel, on the same video memory.
//
//   ./render_test build/arkanoid.rom 300
//
// The reference machine's TMS9918A is a separate implementation (used by
// the Oh Shit! port), so a disagreement here is a real finding. The two
// share only the datasheet, so a misreading both make would go unnoticed:
// this checks drawing, not the hardware.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/Renderer.h"
#include "game/Screen.h"
#include "ref/msx1/Machine1.h"


namespace {


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


void
TakeScreen(Machine1& machine, Screen& screen)
{
	screen.Reset();
	memcpy(screen.VramPointer(), machine.Vdp().Vram(), 0x4000);
	for (int index = 0; index < 8; index++)
		screen.SetRegister(index, machine.Vdp().Register(index));
}


int
Compare(const uint8_t* expected, const uint8_t* actual)
{
	int different = 0;
	for (int i = 0; i < Screen::kWidth * Screen::kLines; i++) {
		if (expected[i] != actual[i])
			different++;
	}
	return different;
}


}	// namespace


int
main(int argc, char** argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: render_test <rom> <frames> [-bios dir]\n");
		return 1;
	}
	std::vector<uint8_t> bios;
	std::vector<uint8_t> logo;
	std::vector<uint8_t> rom;
	std::string biosDir = "third_party/cbios";

	// -hold <row> <bit> <first> <count>: the same key scripting as shot.
	struct Hold {
		int		row;
		int		bit;
		long	first;
		long	count;
	};
	std::vector<Hold> holds;
	for (int i = 3; i < argc; i++) {
		std::string a = argv[i];
		if (a == "-bios" && i + 1 < argc)
			biosDir = argv[++i];
		else if (a == "-hold" && i + 4 < argc) {
			Hold hold;
			hold.row = atoi(argv[++i]);
			hold.bit = atoi(argv[++i]);
			hold.first = atol(argv[++i]);
			hold.count = atol(argv[++i]);
			holds.push_back(hold);
		} else {
			fprintf(stderr, "unknown option %s\n", a.c_str());
			return 1;
		}
	}

	if (!ReadFile(biosDir + "/cbios_main_msx1.rom", bios)
		|| !ReadFile(argv[1], rom)) {
		fprintf(stderr, "cannot read the C-BIOS or the ROM\n");
		return 1;
	}
	ReadFile(biosDir + "/cbios_logo_msx1.rom", logo);

	Machine1 machine;
	if (!machine.LoadBios(bios, logo) || !machine.LoadCartridge(rom)) {
		fprintf(stderr, "bad C-BIOS or ROM size\n");
		return 1;
	}
	machine.Reset();
	long frames = atol(argv[2]);
	for (long frame = 0; frame < frames; frame++) {
		for (int row = 0; row < 11; row++) {
			for (int bit = 0; bit < 8; bit++)
				machine.SetKey(row, bit, false);
		}
		for (const Hold& hold : holds) {
			if (frame >= hold.first && frame < hold.first + hold.count)
				machine.SetKey(hold.row, hold.bit, true);
		}
		machine.RunFrame();
	}

	Screen screen;
	TakeScreen(machine, screen);

	std::vector<uint8_t> ours(static_cast<size_t>(Screen::kWidth)
		* Screen::kLines);
	std::vector<uint8_t> oursAll(ours.size());

	Renderer::Render(screen, ours.data(), true);
	Renderer::Render(screen, oursAll.data(), false);

	const uint8_t* reference = machine.Vdp().Frame();
	int different = Compare(reference, ours.data());
	int differentAll = Compare(reference, oursAll.data());

	printf("%ld frames: %d of %d pixels differ with the 4 sprite limit,"
		" %d with every sprite shown\n", frames, different,
		Screen::kWidth * Screen::kLines, differentAll);

	if (different != 0) {
		// The first differences, to see whether it is a table base, a
		// colour or the sprites.
		int shown = 0;
		for (int i = 0; i < Screen::kWidth * Screen::kLines && shown < 8; i++) {
			if (reference[i] == ours[i])
				continue;
			printf("  (%d,%d) reference %d, ours %d\n", i % Screen::kWidth,
				i / Screen::kWidth, reference[i], ours[i]);
			shown++;
		}
		return 1;
	}
	return 0;
}
