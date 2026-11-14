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



// The sixteen levels, measured on real chips and scaled so the loudest is
// full scale. They are not a straight 1.5 dB ladder: the top of the range is
// about 2 dB a step and the bottom about 3.5, which is why the quiet end of an
// envelope sounds like it does. These are the same numbers the reference
// machine's PSG uses, so the two can be compared.
const float kLevels[16] = {
	0.0f, 0.0137f, 0.0205f, 0.0291f, 0.0423f, 0.0618f, 0.0847f, 0.1369f,
	0.1691f, 0.2647f, 0.3527f, 0.4499f, 0.5704f, 0.6873f, 0.8482f, 1.0f
};


float
Level(int value)
{
	if (value < 0)
		value = 0;
	else if (value > 15)
		value = 15;
	return kLevels[value];
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
	fEnvelopeUp = false;
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
		fEnvelopeUp = (value & 0x04) != 0;
		fEnvelopeStep = 0;
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


// One step of the envelope, at the rate the envelope period sets.
//
// The step counter always counts up, and the attack bit only says which end of
// it the level is read from - 15 down to 0 for a decay, 0 up to 15 for an
// attack. Sixteen steps make a cycle, and the four shape bits then decide what
// the next cycle does, in this order:
//
//   no continue   stop, at silence  (00-03 fall and stay down, 04-07 and 0F
//                                    rise and then drop to silence)
//   hold          stop, at the far end the surviving direction points to
//                 (09 and 0D hold where they got to; 0B reverses first, which
//                 is why it jumps to full volume and stays there)
//   alternate     turn round and run the next cycle the other way
//   neither       run the same cycle again (08 saws down, 0C saws up)
//
// This is the datasheet's own reading of the shapes, and it is the one the
// reference machine implements, so the two agree shape for shape.
void
PsgSynth::_StepEnvelope()
{
	if (fEnvelopeHolding)
		return;

	if (++fEnvelopeStep < 16)
		return;

	const uint8_t shape = fRegisters[13] & 0x0f;
	const bool continuing = (shape & 0x08) != 0;
	const bool alternate = (shape & 0x02) != 0;
	const bool hold = (shape & 0x01) != 0;

	// A hold is a level, not a counter position, so it has to be put back
	// through the read-out: the step that shows level L is L going up and
	// 15 - L coming down.
	if (!continuing) {
		fEnvelopeHolding = true;
		fEnvelopeStep = fEnvelopeUp ? 0 : 15;	// level 0 either way round
		return;
	}

	if (hold) {
		fEnvelopeHolding = true;
		const int level = (alternate ? !fEnvelopeUp : fEnvelopeUp) ? 15 : 0;
		fEnvelopeStep = fEnvelopeUp ? level : 15 - level;
		return;
	}

	if (alternate)
		fEnvelopeUp = !fEnvelopeUp;
	fEnvelopeStep = 0;
}


// The level the envelope is presenting: the same step read from whichever end
// the shape is running towards.
static inline int
EnvelopeLevel(int step, bool up)
{
	return up ? step : 15 - step;
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

	// The envelope period is the length of a whole sixteen-step cycle, so one
	// step lasts 16 * EP / clock and the step rate is clock / (16 * EP). The
	// datasheet's "counts down by 256, then by EP" is the cycle rate:
	// clock / (256 * EP), which is this divided by the sixteen steps.
	int envelopePeriod = fRegisters[11] | (fRegisters[12] << 8);
	if (envelopePeriod == 0)
		envelopePeriod = 1;
	const double envelopeHz = kClock / (kToneDivider * envelopePeriod);

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
				? Level(EnvelopeLevel(fEnvelopeStep, fEnvelopeUp))
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
