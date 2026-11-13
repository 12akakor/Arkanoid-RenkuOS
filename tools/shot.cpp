/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Runs the original Arkanoid ROM on the reference MSX1 and writes the
// picture and, on request, the video memory and the game variables.
//
//   ./shot arkanoid.rom 120 build/title.png
//   ./shot arkanoid.rom 120 build/title.png -vram build/title.vram
//
// The ROM is built from the annotated disassembly by tools/build_rom.sh,
// which also checks its checksum.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "ref/msx1/Machine1.h"
#include "ref/video/Tms9918.h"
#include "png.h"


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


bool
WriteFile(const std::string& path, const uint8_t* data, size_t size)
{
	FILE* file = fopen(path.c_str(), "wb");
	if (file == NULL)
		return false;
	size_t written = fwrite(data, 1, size, file);
	fclose(file);
	return written == size;
}


void
WritePicture(const std::string& path, const Tms9918& vdp)
{
	const uint8_t* frame = vdp.Frame();
	std::vector<uint8_t> rgb(static_cast<size_t>(Tms9918::kWidth)
		* Tms9918::kLines * 3);
	for (int i = 0; i < Tms9918::kWidth * Tms9918::kLines; i++) {
		uint32_t colour = Tms9918::Rgb(frame[i]);
		rgb[i * 3 + 0] = static_cast<uint8_t>((colour >> 16) & 0xff);
		rgb[i * 3 + 1] = static_cast<uint8_t>((colour >> 8) & 0xff);
		rgb[i * 3 + 2] = static_cast<uint8_t>(colour & 0xff);
	}
	if (!png::WritePng(path.c_str(), rgb.data(), Tms9918::kWidth,
			Tms9918::kLines))
		fprintf(stderr, "cannot write %s\n", path.c_str());
}


}	// namespace


int
main(int argc, char** argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: shot <rom> <frames> <out.png>"
			" [-bios dir] [-vram file] [-ram file]"
			" [-hold row bit first count]\n");
		return 1;
	}
	std::string romPath = argv[1];
	long frames = atol(argv[2]);
	std::string outPath = argv[3];
	std::string biosDir = "third_party/cbios";
	std::string vramPath;
	std::string ramPath;

	// A key held for a range of frames: -hold <row> <bit> <first> <count>.
	// The MSX keyboard matrix has the cursor keys and SPACE on row 8.
	struct Hold {
		int		row;
		int		bit;
		long	first;
		long	count;
	};
	std::vector<Hold> holds;

	for (int i = 4; i < argc; i++) {
		std::string a = argv[i];
		if (i + 1 >= argc) {
			fprintf(stderr, "missing value for %s\n", a.c_str());
			return 1;
		}
		if (a == "-bios")
			biosDir = argv[++i];
		else if (a == "-vram")
			vramPath = argv[++i];
		else if (a == "-ram")
			ramPath = argv[++i];
		else if (a == "-hold") {
			if (i + 4 >= argc) {
				fprintf(stderr, "missing values for -hold\n");
				return 1;
			}
			Hold hold;
			hold.row = atoi(argv[++i]);
			hold.bit = atoi(argv[++i]);
			hold.first = atol(argv[++i]);
			hold.count = atol(argv[++i]);
			holds.push_back(hold);
		}
		else {
			fprintf(stderr, "unknown option %s\n", a.c_str());
			return 1;
		}
	}

	std::vector<uint8_t> bios;
	std::vector<uint8_t> logo;
	std::vector<uint8_t> rom;
	if (!ReadFile(biosDir + "/cbios_main_msx1.rom", bios)
		|| !ReadFile(romPath, rom)) {
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

	WritePicture(outPath, machine.Vdp());

	printf("frames %ld  pc %04X  slots %02X  vdp r0-r7",
		frames, machine.Cpu().r.pc, machine.SlotRegister());
	for (int reg = 0; reg < 8; reg++)
		printf(" %02X", machine.Vdp().Register(reg));
	printf("\n");

	// The game keeps everything in 0xE000-0xE5FF; a few of the variables
	// tell whether it reached the title screen or a level.
	const uint8_t* ram = machine.Ram();
	printf("0xE00A transition %02X  0xE00B state %02X  0xE01B level %02X"
		"  0xE01D lives %02X  0xE53C title action %02X\n",
		ram[0xE00A], ram[0xE00B], ram[0xE01B], ram[0xE01D], ram[0xE53C]);

	if (!vramPath.empty()
		&& !WriteFile(vramPath, machine.Vdp().Vram(), 0x4000))
		fprintf(stderr, "cannot write %s\n", vramPath.c_str());
	if (!ramPath.empty() && !WriteFile(ramPath, ram, 0x10000))
		fprintf(stderr, "cannot write %s\n", ramPath.c_str());
	return 0;
}
