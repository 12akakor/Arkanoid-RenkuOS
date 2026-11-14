/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */

// The chip, from the registers.
//
// The driver in SoundPlayer produces the sixteen register values an
// AY-3-8910 would be holding; this turns those into samples. It is the same
// arithmetic the chip does, run at the sample rate instead of the clock: three
// square-wave tone counters, one noise counter clocked through a seventeen-bit
// shift register, one envelope counter, and the mixer byte that decides which
// of them each channel hears.


#ifndef ARKANOID_PSG_SYNTH_H
#define ARKANOID_PSG_SYNTH_H


#include <stddef.h>
#include <stdint.h>


class PsgSynth {
public:
	// The MSX's PSG runs from half the 3.579545 MHz crystal.
	static const double kClock;

						PsgSynth(int sampleRate = 44100);

	// Everything back to silence, as after a reset.
	void				Reset();

	// A register write, exactly as the driver makes them: register 0..15.
	void				WritePsg(uint8_t reg, uint8_t value);

	// What the chip would hand back. The driver reads register 7 after it
	// takes the mixer over, and the game's paddle routine reads 14 and 15.
	uint8_t				ReadPsg(uint8_t reg) const;

	// Interleaved stereo, signed 16-bit. Both channels carry the same signal:
	// the MSX's PSG is mono and the game never asked for anything else.
	void				Render(int16_t* samples, size_t frames);

			int			SampleRate() const { return fSampleRate; }

private:
	void				_StepEnvelope();
	double				_ToneHz(int channel) const;

			int			fSampleRate;
			uint8_t		fRegisters[16];

			double		fTonePhase[3];
			double		fNoisePhase;
			double		fEnvelopePhase;

			uint32_t	fNoise;
			int			fEnvelopeStep;	// 0..15, always counted up
			bool		fEnvelopeUp;	// which end of the step the level is read from
			bool		fEnvelopeHolding;
};


#endif	// ARKANOID_PSG_SYNTH_H
