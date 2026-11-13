/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The video memory and the registers of the MSX1 VDP, as Arkanoid uses them.
// This is the game's own picture, not an emulated chip: the routines write
// video memory through these helpers exactly where the original writes it
// through the BIOS, and Renderer turns the result into pixels.


#ifndef ARKANOID_SCREEN_H
#define ARKANOID_SCREEN_H

#include <stdint.h>


class Screen {
public:
	enum {
		kVramSize = 0x4000,
		kWidth = 256,
		kLines = 192,
		kRegisters = 8
	};

								Screen();

			void				Reset();

	// The eight write-only registers.
	inline	uint8_t				Register(int index) const;
			void				SetRegister(int index, uint8_t value);

	// Video memory.
	inline	uint8_t				Vram(uint16_t address) const;
			void				SetVram(uint16_t address, uint8_t value);
	// FILVRM of the BIOS: fills 'count' bytes.
			void				FillVram(uint16_t address, uint8_t value,
									int count);
	// LDIRVM of the BIOS: copies 'count' bytes, wrapping inside 16 KB.
			void				CopyToVram(uint16_t address,
									const uint8_t* source, int count);
	inline	const uint8_t*		VramPointer() const;
	inline	uint8_t*			VramPointer();

	// Table bases, from the registers as the MSX documentation gives them.
	// With the values this game sets (R2 = 0x06, R3 = 0xFF, R4 = 0x03,
	// R5 = 0x36, R6 = 0x07) these are 0x1800, 0x2000, 0x0000, 0x1B00 and
	// 0x3800, the same five addresses as the game's own VDP_BASE_POINTERS.
	inline	uint16_t			NameBase() const;
	inline	uint16_t			ColourBase() const;
	inline	uint16_t			PatternBase() const;
	inline	uint16_t			SpriteAttributeBase() const;
	inline	uint16_t			SpritePatternBase() const;

	inline	bool				DisplayEnabled() const;
	inline	bool				SpritesAre16() const;
	inline	bool				SpritesMagnified() const;
	inline	uint8_t				Backdrop() const;

private:
			uint8_t				fVram[kVramSize];
			uint8_t				fRegisters[kRegisters];
};


inline uint8_t
Screen::Register(int index) const
{
	return fRegisters[index & 7];
}


inline uint8_t
Screen::Vram(uint16_t address) const
{
	return fVram[address & (kVramSize - 1)];
}


inline const uint8_t*
Screen::VramPointer() const
{
	return fVram;
}


inline uint8_t*
Screen::VramPointer()
{
	return fVram;
}


inline uint16_t
Screen::NameBase() const
{
	return static_cast<uint16_t>((fRegisters[2] & 0x0f) << 10);
}


inline uint16_t
Screen::ColourBase() const
{
	return static_cast<uint16_t>((fRegisters[3] & 0x80) << 6);
}


inline uint16_t
Screen::PatternBase() const
{
	return static_cast<uint16_t>((fRegisters[4] & 0x04) << 11);
}


inline uint16_t
Screen::SpriteAttributeBase() const
{
	return static_cast<uint16_t>((fRegisters[5] & 0x7f) << 7);
}


inline uint16_t
Screen::SpritePatternBase() const
{
	return static_cast<uint16_t>((fRegisters[6] & 0x07) << 11);
}


inline bool
Screen::DisplayEnabled() const
{
	return (fRegisters[1] & 0x40) != 0;
}


inline bool
Screen::SpritesAre16() const
{
	return (fRegisters[1] & 0x02) != 0;
}


inline bool
Screen::SpritesMagnified() const
{
	return (fRegisters[1] & 0x01) != 0;
}


inline uint8_t
Screen::Backdrop() const
{
	return fRegisters[7] & 0x0f;
}


#endif	// ARKANOID_SCREEN_H
