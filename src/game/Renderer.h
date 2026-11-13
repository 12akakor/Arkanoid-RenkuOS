/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Turns the video memory into the 256x192 picture, in the 16 fixed colours
// of the MSX1. Graphic 2 only: that is the mode Arkanoid uses for both the
// title screen and the game, with 16x16 sprites.
//
// In Graphic 2 every character cell has a foreground and a background
// colour per pixel row, and the pattern table holds three 2 KB blocks, one
// for each third of the screen. Sprites are separate: 32 of them, 16x16,
// with one colour each and colour 0 meaning transparent.


#ifndef ARKANOID_RENDERER_H
#define ARKANOID_RENDERER_H

#include <stdint.h>

#include "Screen.h"


class Renderer {
public:
	// Draws the picture into 'frame' (256x192, one colour index per byte).
	// 'spriteLimit' keeps the 4 sprites per line of the real chip; off,
	// every sprite shows, which is what the game gets on a modern screen.
	static	void				Render(const Screen& screen, uint8_t* frame,
									bool spriteLimit);

	// The TMS9918A's fixed colours, as RGB.
	static	uint32_t			Rgb(int index);

private:
	static	void				_RenderSprites(const Screen& screen,
									uint8_t* frame, bool spriteLimit);
};


#endif	// ARKANOID_RENDERER_H
