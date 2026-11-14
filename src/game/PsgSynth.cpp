/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "PsgSynth.h"

#include <math.h>
#include <string.h>


const double PsgSynth::kClock = 1789772.5;

namespace {

// The chip divides its clock by sixteen for the tone counters and for the
// noise counter, and by 256 for the envelope.
const int kToneDivider = 16;
const int kNoiseDivider = 16;
const int kEnvelopeDivider = 256;


// The AY's sixteen levels are about 1.5 dB apart, which is a quarter of an
// octave each, and the loudest is full scale.
float
Level(int value)
{
	if (value <= 0)
		return 0.0f;
	return powf(2.0f, (value - 15) * 0.25f);
}

}


PsgSynth::PsgSynth(int sampleRate)
	:
	fSampleRate(sampleRate > 0 ? sampleRate : 44100)
{
	Reset();
}


void
PsgSynth::Reset()
{
	memset(fRegisters, 0, sizeof(fRegisters));
	for (int channel = 0; channel < 3; channel++)
		fTonePhase[channel] = 0.0;

	fNoisePhase = 0.0;
	fEnvelopePhase = 0.0;
	fNoise = 1;
	fEnvelopeStep = 0;
	fEnvelopeDirection = 1;
	fEnvelopeHolding = false;
}


void
PsgSynth::WritePsg(uint8_t reg, uint8_t value)
{
	reg &= 15;
	fRegisters[reg] = value;

	// A new shape restarts the envelope: the attack bit says which end it
	// starts from, and shape 0 stops it dead because every bit is clear.
	if (reg == 13) {
		fEnvelopeDirection = (value & 0x04) != 0 ? 1 : -1;
		fEnvelopeStep = fEnvelopeDirection > 0 ? 0 : 15;
		fEnvelopePhase = 0.0;
		fEnvelopeHolding = false;
	}
}


uint8_t
PsgSynth::ReadPsg(uint8_t reg) const
{
	return fRegisters[reg & 15];
}


double
PsgSynth::_ToneHz(int channel) const
{
	int period = fRegisters[channel * 2]
		| ((fRegisters[channel * 2 + 1] & 0x0f) << 8);
	if (period == 0)
		period = 1;
	return kClock / (kToneDivider * period);
}


// One step of the envelope, at the rate the envelope period sets. The four
// shape bits are continue, attack, alternate and hold, and they do what their
// names say: a shape that does not continue stops at the end of its first
// ramp, one that alternates turns round instead, and one that holds stays at
// the level it reached.
void
PsgSynth::_StepEnvelope()
{
	if (fEnvelopeHolding)
		return;

	if (fEnvelopeDirection > 0) {
		if (fEnvelopeStep < 15) {
			++fEnvelopeStep;
			return;
		}
	} else {
		if (fEnvelopeStep > 0) {
			--fEnvelopeStep;
			return;
		}
	}

	const uint8_t shape = fRegisters[13] & 0x0f;
	const bool continuing = (shape & 0x08) != 0;
	const bool alternate = (shape & 0x02) != 0;
	const bool hold = (shape & 0x01) != 0;

	if (!continuing || (hold && !alternate)) {
		fEnvelopeHolding = true;
		return;
	}

	if (alternate)
		fEnvelopeDirection = -fEnvelopeDirection;
}


void
PsgSynth::Render(int16_t* samples, size_t frames)
{
	if (samples == NULL)
		return;

	const uint8_t mixer = fRegisters[7];

	double toneHz[3];
	for (int channel = 0; channel < 3; channel++)
		toneHz[channel] = _ToneHz(channel);

	int noisePeriod = fRegisters[6] & 0x1f;
	if (noisePeriod == 0)
		noisePeriod = 1;
	const double noiseHz = kClock / (kNoiseDivider * noisePeriod);

	int envelopePeriod = fRegisters[11] | (fRegisters[12] << 8);
	if (envelopePeriod == 0)
		envelopePeriod = 1;
	const double envelopeHz = kClock / (kEnvelopeDivider * envelopePeriod);

	for (size_t frame = 0; frame < frames; frame++) {
		// The noise counter and the envelope counter run once for the whole
		// chip, not once per channel.
		fNoisePhase += noiseHz / fSampleRate;
		while (fNoisePhase >= 1.0) {
			fNoisePhase -= 1.0;
			const uint32_t bit = (fNoise ^ (fNoise >> 3)) & 1;
			fNoise = (fNoise >> 1) | (bit << 16);
		}

		fEnvelopePhase += envelopeHz / fSampleRate;
		while (fEnvelopePhase >= 1.0) {
			fEnvelopePhase -= 1.0;
			_StepEnvelope();
		}

		const bool noiseBit = (fNoise & 1) != 0;

		float sum = 0.0f;
		for (int channel = 0; channel < 3; channel++) {
			fTonePhase[channel] += toneHz[channel] / fSampleRate;
			while (fTonePhase[channel] >= 1.0)
				fTonePhase[channel] -= 1.0;
			const bool toneBit = fTonePhase[channel] < 0.5;

			// The mixer bits are active low: a clear bit lets that source
			// through. With both clear the channel is the AND of the two,
			// which is how the chip mixes them.
			const bool toneOn = (mixer & (1 << channel)) == 0;
			const bool noiseOn = (mixer & (8 << channel)) == 0;

			const uint8_t control = fRegisters[8 + channel];
			const float amplitude = (control & 0x10) != 0
				? Level(fEnvelopeStep)
				: Level(control & 0x0f);

			bool on;
			if (toneOn && noiseOn)
				on = toneBit && noiseBit;
			else if (toneOn)
				on = toneBit;
			else if (noiseOn)
				on = noiseBit;
			else
				on = true;

			// A square wave is symmetric about silence once the coupling
			// capacitor has taken the chip's DC away, so the two states are
			// plus and minus the level rather than level and nothing.
			if (on)
				sum += amplitude;
			else
				sum -= amplitude;
		}

		// Three channels at full scale would be three times the range.
		float value = sum / 3.0f;
		if (value > 1.0f)
			value = 1.0f;
		else if (value < -1.0f)
			value = -1.0f;

		const int16_t sample = static_cast<int16_t>(value * 32767.0f);
		samples[frame * 2] = sample;
		samples[frame * 2 + 1] = sample;
	}
}
