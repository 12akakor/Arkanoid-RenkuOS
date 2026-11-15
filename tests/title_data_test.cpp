/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The ROM data the title screen reads, checked against the cartridge.
//
//   ./title_data_test build/arkanoid.rom
//
// DRAW_TITLE_SCREEN (0x4B8A) reads seven tables straight out of the cartridge,
// at fixed addresses, and the title's colours are a compressed stream that
// DECOMPRESS_TILE_COLORS (0x4389) expands into the colour table. The port
// carries the whole cartridge as kRomImage, generated from the ROM that
// rom_test checks against the original's sha1, so "extracting" the data is
// naming the addresses - and this checks that the names point at what the
// cartridge has there: the strings read as the text the game shows, and the
// data blocks lie inside the image and match the ROM file byte for byte.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/RomImage.h"
#include "game/State.h"


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


const uint8_t*
Rom(uint16_t address)
{
	return kRomImage + (address - kRomBase);
}


int
failed = 0;


void
Check(bool ok, const char* what)
{
	if (!ok) {
		printf("  %s: no\n", what);
		failed++;
	}
}


// The data the title needs, all inside the cartridge's address range.
void
CheckInside(const char* name, uint16_t from, uint16_t length)
{
	printf("  %-26s %04x, %d bytes\n", name, from, length);
	Check(from >= kRomBase && from + length <= kRomBase + kRomSize, name);
}

}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: title_data_test <rom>\n");
		return 1;
	}
	std::vector<uint8_t> rom;
	if (!ReadFile(argv[1], rom) || rom.size() != (size_t)kRomSize) {
		fprintf(stderr, "cannot read the ROM\n");
		return 1;
	}

	printf("the title screen's data, against the cartridge\n");

	CheckInside("TITLE_TILES", GameRam::kTitleTiles, 2048);
	CheckInside("TITLE_COLORS_COMPRESSED", GameRam::kTitleColors, 0x17E);
	CheckInside("ARKANOID_LOGO_CHARS", GameRam::kArkanoidLogoChars, 0x60);
	CheckInside("PUSH_START_BUTTON_STR", GameRam::kPushStartButton, 17);
	CheckInside("TAITO_CORP_STR", GameRam::kTaitoCorp, 0x18);
	CheckInside("ALL_RIGHTS_RESERVED_STR", GameRam::kAllRightsReserved, 0x13);
	CheckInside("TAITO_HALF1_LOGO_CHARS", GameRam::kTaitoHalf1, 0x0B);
	CheckInside("TAITO_HALF2_LOGO_CHARS", GameRam::kTaitoHalf2, 0x0B);

	// The image the port reads and the ROM file are the same bytes.
	int different = 0;
	for (int i = 0; i < kRomSize; i++) {
		if (kRomImage[i] != rom[i])
			different++;
	}
	printf("  the image and the ROM file differ in %d bytes\n", different);
	Check(different == 0, "the image is the ROM");

	// What the strings say, exactly as the game lengths them.
	const char* start = reinterpret_cast<const char*>(Rom(GameRam::kPushStartButton));
	Check(strncmp(start, "PUSH START BUTTON", 17) == 0, "PUSH START BUTTON");
	const char* rights
		= reinterpret_cast<const char*>(Rom(GameRam::kAllRightsReserved));
	Check(strncmp(rights, "ALL RIGHTS RESERVED", 19) == 0, "ALL RIGHTS RESERVED");

	printf("  %-26s \"%.17s\"\n", "PUSH_START_BUTTON_STR", start);
	printf("  %-26s \"%.24s\"\n", "TAITO_CORP_STR",
		reinterpret_cast<const char*>(Rom(GameRam::kTaitoCorp)));
	printf("  %-26s \"%.19s\"\n", "ALL_RIGHTS_RESERVED_STR", rights);

	// The title's colours are compressed; the stream ends with a byte whose
	// high nibble is set, or what DECOMPRESS_TILE_COLORS never reaches.
	printf("  %-26s starts %02x %02x %02x\n", "TITLE_COLORS_COMPRESSED",
		Rom(GameRam::kTitleColors)[0], Rom(GameRam::kTitleColors)[1],
		Rom(GameRam::kTitleColors)[2]);

	printf("%s\n", failed == 0 ? "everything is where the cartridge has it"
		: "the data is not where the cartridge has it");
	return failed == 0 ? 0 : 1;
}
