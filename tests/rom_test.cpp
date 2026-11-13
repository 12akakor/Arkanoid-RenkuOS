/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Checks that the cartridge built into the program is the ROM file, byte for
// byte, so the game plays the same with and without a ROM present.
//
//   ./rom_test build/arkanoid.rom
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/RomImage.h"


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


}	// namespace


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: rom_test <rom>\n");
		return 1;
	}

	std::vector<uint8_t> rom;
	if (!ReadFile(argv[1], rom)) {
		fprintf(stderr, "cannot read %s\n", argv[1]);
		return 1;
	}
	if (rom.size() != static_cast<size_t>(kRomSize)) {
		fprintf(stderr, "%s is %zu bytes, expected %d\n", argv[1], rom.size(),
			kRomSize);
		return 1;
	}

	int different = 0;
	int first = -1;
	for (int i = 0; i < kRomSize; i++) {
		if (rom[i] == kRomImage[i])
			continue;
		if (first < 0)
			first = i;
		different++;
	}

	if (different != 0) {
		printf("the built-in cartridge is not the ROM: %d bytes differ,"
			" first at 0x%04x\n", different, kRomBase + first);
		return 1;
	}

	printf("the built-in cartridge equals %s (%d bytes)\n", argv[1],
		kRomSize);
	return 0;
}
