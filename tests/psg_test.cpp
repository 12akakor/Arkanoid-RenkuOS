/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The synthesiser, measured rather than listened to.
//
//   ./psg_test
//
// There is no sound card here, so every claim about the chip has to be a
// number: a tone's frequency is the count of sign changes in a second, a
// silent register image is a buffer of zeros, and an envelope is a ramp whose
// end can be read off the samples.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "game/PsgSynth.h"


namespace {

const int kRate = 44100;

int gChecks = 0;
int gFailures = 0;


void
Check(bool condition, const char* what, double got, double expected)
{
	gChecks++;
	if (condition) {
		printf("  ok    %-44s %8.2f (expected %8.2f)\n", what, got, expected);
		return;
	}
	gFailures++;
	printf("  FAIL  %-44s %8.2f (expected %8.2f)\n", what, got, expected);
}


std::vector<int16_t>
Render(PsgSynth& synth, int frames)
{
	std::vector<int16_t> samples(static_cast<size_t>(frames) * 2);
	synth.Render(samples.data(), frames);
	return samples;
}


// Sign changes in the left channel: two for every cycle of a square wave.
int
Crossings(const std::vector<int16_t>& samples)
{
	int crossings = 0;
	int last = 0;
	for (size_t frame = 0; frame < samples.size() / 2; frame++) {
		const int16_t value = samples[frame * 2];
		const int sign = value > 0 ? 1 : (value < 0 ? -1 : 0);
		if (sign == 0)
			continue;
		if (last != 0 && sign != last)
			crossings++;
		last = sign;
	}
	return crossings;
}


// The peak of the last 'frames' frames: a ramp is judged by where it ends,
// not by where the buffer happened to start.
double
PeakTail(const std::vector<int16_t>& samples, size_t frames)
{
	const size_t total = samples.size() / 2;
	if (frames > total)
		frames = total;
	double peak = 0.0;
	for (size_t frame = total - frames; frame < total; frame++) {
		const double value = fabs(static_cast<double>(samples[frame * 2]));
		if (value > peak)
			peak = value;
	}
	return peak;
}


double
Peak(const std::vector<int16_t>& samples)
{
	double peak = 0.0;
	for (size_t frame = 0; frame < samples.size() / 2; frame++) {
		const double value = fabs(static_cast<double>(samples[frame * 2]));
		if (value > peak)
			peak = value;
	}
	return peak;
}


// A tone on one channel only: mixer bits are active low, so the other tone
// bits and every noise bit are set, and only this channel's are clear.
void
SingleTone(PsgSynth& synth, int channel, int period, int volume)
{
	synth.Reset();

	uint8_t mixer = 0x3f;
	mixer &= static_cast<uint8_t>(~(1 << channel));
	synth.WritePsg(7, mixer);

	synth.WritePsg(static_cast<uint8_t>(channel * 2), period & 0xff);
	synth.WritePsg(static_cast<uint8_t>(channel * 2 + 1),
		static_cast<uint8_t>(period >> 8));
	for (int other = 0; other < 3; other++)
		synth.WritePsg(static_cast<uint8_t>(8 + other),
			other == channel ? static_cast<uint8_t>(volume) : 0);
}

}


int
main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	printf("the PSG, sixteen registers in and samples out\n");

	PsgSynth synth(kRate);

	// Nothing written yet: the chip is at reset and makes no sound.
	{
		std::vector<int16_t> samples = Render(synth, kRate / 10);
		Check(Peak(samples) == 0.0, "a fresh chip is silent", Peak(samples), 0.0);
	}

	// The registers read back what was written, which is what the driver's
	// register-7 handshake depends on.
	{
		synth.WritePsg(7, 0x38);
		Check(synth.ReadPsg(7) == 0x38, "register 7 reads back", synth.ReadPsg(7),
			0x38);
	}

	// A tone of 254 periods is 440 Hz off a 1789772.5 Hz clock.
	{
		SingleTone(synth, 0, 254, 15);
		const double expected = PsgSynth::kClock / (16.0 * 254.0);
		std::vector<int16_t> samples = Render(synth, kRate);
		const double measured = Crossings(samples) / 2.0;
		Check(fabs(measured - expected) < expected * 0.01,
			"channel A, period 254, is 440 Hz", measured, expected);
	}

	// The other two channels are their own counters, not copies of A.
	{
		SingleTone(synth, 1, 127, 15);
		const double expected = PsgSynth::kClock / (16.0 * 127.0);
		std::vector<int16_t> samples = Render(synth, kRate);
		const double measured = Crossings(samples) / 2.0;
		Check(fabs(measured - expected) < expected * 0.01,
			"channel B, period 127, is twice that", measured, expected);
	}

	{
		SingleTone(synth, 2, 508, 15);
		const double expected = PsgSynth::kClock / (16.0 * 508.0);
		std::vector<int16_t> samples = Render(synth, kRate);
		const double measured = Crossings(samples) / 2.0;
		Check(fabs(measured - expected) < expected * 0.01,
			"channel C, period 508, is half of A", measured, expected);
	}

	// Volume 0 is silence, whatever the tone is doing.
	{
		SingleTone(synth, 0, 254, 0);
		std::vector<int16_t> samples = Render(synth, kRate / 10);
		Check(Peak(samples) == 0.0, "volume 0 is silence", Peak(samples), 0.0);
	}

	// The noise generator: tone off for channel A, noise on.
	{
		synth.Reset();
		synth.WritePsg(7, 0x37);	// tone A/B/C off, noise A on
		synth.WritePsg(6, 1);		// the fastest the noise counter goes
		synth.WritePsg(8, 15);
		std::vector<int16_t> samples = Render(synth, kRate / 10);
		const double peak = Peak(samples);
		Check(peak > 0.0, "noise makes a sound of its own", peak, 0.0);
	}

	// The envelope, attack then hold: the level has to climb and stay up.
	{
		synth.Reset();
		synth.WritePsg(7, 0x3e);	// tone A only
		synth.WritePsg(0, 1);		// a tone far above the sample rate
		synth.WritePsg(1, 0);
		synth.WritePsg(8, 0x10);	// channel A follows the envelope
		synth.WritePsg(11, 1);		// envelope period 1: 16 steps at once
		synth.WritePsg(12, 0);
		synth.WritePsg(13, 0x04);	// attack, no continue, hold

		std::vector<int16_t> early = Render(synth, 32);
		std::vector<int16_t> late = Render(synth, 512);
		Check(Peak(late) > Peak(early), "the attack envelope climbs",
			Peak(late), Peak(early) + 1.0);
	}

	// Shape 0 decays from 15 and holds at nothing.
	{
		synth.Reset();
		synth.WritePsg(7, 0x3e);
		synth.WritePsg(0, 1);
		synth.WritePsg(1, 0);
		synth.WritePsg(8, 0x10);
		synth.WritePsg(11, 1);
		synth.WritePsg(12, 0);
		synth.WritePsg(13, 0x00);	// decay, stop

		std::vector<int16_t> early = Render(synth, 32);
		std::vector<int16_t> late = Render(synth, 512);
		Check(Peak(early) > 0.0, "the decay envelope starts loud", Peak(early),
			1.0);
		Check(PeakTail(late, 128) == 0.0, "the decay envelope falls to nothing",
			PeakTail(late, 128), 0.0);
	}

	printf("%d of %d checks pass\n", gChecks - gFailures, gChecks);
	return gFailures == 0 ? 0 : 1;
}
