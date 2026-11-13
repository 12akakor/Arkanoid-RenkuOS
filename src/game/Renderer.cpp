/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Renderer, see Renderer.h.
#include "Renderer.h"

#include <string.h>


namespace {

// The colours of the TMS9918A, as the MSX1 shows them.
const uint32_t kPalette[16] = {
	0x000000, 0x000000, 0x21c842, 0x5edc78, 0x5455ed, 0x7d76fc, 0xd4524d,
	0x42ebf5, 0xfc5554, 0xff7978, 0xd4c154, 0xe6ce80, 0x21b03b, 0xc95bba,
	0xcccccc, 0xffffff
};

}


void
Renderer::Render(const Screen& screen, uint8_t* frame, bool spriteLimit)
{
	const uint8_t backdrop = screen.Backdrop();
	if (!screen.DisplayEnabled()) {
		memset(frame, backdrop, Screen::kWidth * Screen::kLines);
		return;
	}

	const uint16_t nameBase = screen.NameBase();
	const uint16_t patternBase = screen.PatternBase();
	const uint16_t colourBase = screen.ColourBase();

	for (int row = 0; row < 24; row++) {
		const int third = row >> 3;			// one 2 KB block each
		const uint16_t nameAddress = nameBase + row * 32;
		for (int line = 0; line < 8; line++) {
			uint8_t* out = frame + (row * 8 + line) * Screen::kWidth;
			for (int column = 0; column < 32; column++) {
				const uint8_t name = screen.Vram(nameAddress + column);
				const uint16_t offset = third * 0x800 + name * 8 + line;
				const uint8_t bits = screen.Vram(patternBase + offset);
				const uint8_t colour = screen.Vram(colourBase + offset);
				uint8_t foreground = colour >> 4;
				uint8_t background = colour & 0x0f;
				if (foreground == 0)
					foreground = backdrop;
				if (background == 0)
					background = backdrop;
				for (int bit = 0; bit < 8; bit++) {
					out[column * 8 + bit] = (bits & (0x80 >> bit)) != 0
						? foreground : background;
				}
			}
		}
	}

	_RenderSprites(screen, frame, spriteLimit);
}


void
Renderer::_RenderSprites(const Screen& screen, uint8_t* frame,
	bool spriteLimit)
{
	const uint16_t attributeBase = screen.SpriteAttributeBase();
	const uint16_t patternBase = screen.SpritePatternBase();
	const int size = screen.SpritesAre16() ? 16 : 8;
	const int scale = screen.SpritesMagnified() ? 2 : 1;
	const int height = size * scale;

	// Which sprite owns each pixel of a line; the lowest number wins, as on
	// the real chip.
	uint8_t owner[Screen::kWidth];

	for (int line = 0; line < Screen::kLines; line++) {
		uint8_t* out = frame + line * Screen::kWidth;
		memset(owner, 0, sizeof(owner));
		int visible = 0;
		for (int index = 0; index < 32; index++) {
			const uint16_t attributes = attributeBase + index * 4;
			const uint8_t y = screen.Vram(attributes + 0);
			if (y == 208)
				break;
			const int top = static_cast<int>(y) + 1;
			int row = line - top;
			if (row < 0 || row >= height)
				continue;
			if (spriteLimit && visible >= 4)
				break;
			visible++;

			if (scale == 2)
				row >>= 1;
			const uint8_t x = screen.Vram(attributes + 1);
			const uint8_t name = screen.Vram(attributes + 2);
			const uint8_t colour = screen.Vram(attributes + 3) & 0x0f;
			if (colour == 0)
				continue;

			int left = x;
			if ((screen.Vram(attributes + 3) & 0x80) != 0)
				left -= 32;

			// A 16x16 sprite takes two bytes per pixel row, the second 16
			// bytes after the first, and its pattern number is a multiple
			// of four.
			const uint16_t rowAddress = size == 16
				? static_cast<uint16_t>(patternBase + (name & 0xfc) * 8 + row)
				: static_cast<uint16_t>(patternBase + name * 8 + row);
			const uint8_t leftByte = screen.Vram(rowAddress);
			const uint8_t rightByte = size == 16
				? screen.Vram(rowAddress + 16) : 0;

			for (int i = 0; i < size; i++) {
				const uint8_t byte = i < 8 ? leftByte : rightByte;
				if ((byte & (0x80 >> (i & 7))) == 0)
					continue;
				for (int repeat = 0; repeat < scale; repeat++) {
					const int target = left + i * scale + repeat;
					if (target < 0 || target >= Screen::kWidth)
						continue;
					if (owner[target] != 0)
						continue;
					owner[target] = 1;
					out[target] = colour;
				}
			}
		}
	}
}


uint32_t
Renderer::Rgb(int index)
{
	return kPalette[index & 15];
}
