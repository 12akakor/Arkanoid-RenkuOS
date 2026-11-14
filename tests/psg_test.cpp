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
// silent register image is a buffer of zeros, and an envelope is a sequence of
// levels that can be read off the samples and compared with the datasheet's
// own drawing of the sixteen shapes.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "game/PsgSynth.h"


namespace {

const int kRate = 44100;

// The AY's measured levels, the same numbers the synthesiser uses.
const double kLevels[16] = {
	0.0, 0.0137, 0.0205, 0.0291, 0.0423, 0.0618, 0.0847, 0.1369,
	0.1691, 0.2647, 0.3527, 0.4499, 0.5704, 0.6873, 0.8482, 1.0
};

int gChecks = 0;
int gFailures = 0;


void
Check(bool condition, const char* what, double got, double expected)
{
	gChecks++;
	if (condition) {
		printf("  ok    %-46s %9.2f (expected %9.2f)\n", what, got, expected);
		return;
	}
	gFailures++;
	printf("  FAIL  %-46s %9.2f (expected %9.2f)\n", what, got, expected);
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


std::vector<int>
Ramp(int from, int to)
{
	std::vector<int> values;
	const int step = from <= to ? 1 : -1;
	for (int value = from;; value += step) {
		values.push_back(value);
		if (value == to)
			break;
	}
	return values;
}


std::vector<int>
Repeat(int value, int count)
{
	return std::vector<int>(static_cast<size_t>(count), value);
}


std::vector<int16_t>
Render(PsgSynth& synth, int frames)
{
	std::vector<int16_t> samples(static_cast<size_t>(frames) * 2);
	synth.Render(samples.data(), frames);
	return samples;
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


// The envelope's level in each step, read off the samples: the mean magnitude
// over the middle of each step, scaled against the loudest step and matched to
// the level it represents. The middle is used so that the boundary, where the
// level changes, cannot be mistaken for the level itself.
std::vector<int>
LevelSequence(const std::vector<int16_t>& mono, double stepSamples, int steps)
{
	std::vector<double> means;
	for (int step = 0; step < steps; step++) {
		const double start = stepSamples * step;
		const int from = static_cast<int>(start + stepSamples * 0.3);
		const int to = static_cast<int>(start + stepSamples * 0.7);
		double sum = 0.0;
		int counted = 0;
		for (int frame = from; frame < to && frame < (int)mono.size(); frame++) {
			sum += fabs(static_cast<double>(mono[frame]));
			counted++;
		}
		means.push_back(counted > 0 ? sum / counted : 0.0);
	}

	double loudest = 0.0;
	for (size_t i = 0; i < means.size(); i++) {
		if (means[i] > loudest)
			loudest = means[i];
	}

	std::vector<int> levels;
	for (size_t i = 0; i < means.size(); i++) {
		const double ratio = loudest > 0.0 ? means[i] / loudest : 0.0;
		int level = 0;
		double best = 1e9;
		for (int value = 0; value < 16; value++) {
			const double distance = fabs(kLevels[value] - ratio);
			if (distance < best) {
				best = distance;
				level = value;
			}
		}
		levels.push_back(level);
	}
	return levels;
}


// The envelope's first 'steps' levels, with a period short enough to keep the
// test quick: one step is 16 * EP / clock seconds.
std::vector<int>
EnvelopeLevels(PsgSynth& synth, int shape, int steps)
{
	const int envelopePeriod = 16;

	synth.Reset();
	synth.WritePsg(7, 0x3e);		// tone A only
	synth.WritePsg(0, 1);			// well above the sample rate
	synth.WritePsg(1, 0);
	synth.WritePsg(8, 0x10);		// channel A follows the envelope
	synth.WritePsg(11, envelopePeriod & 0xff);
	synth.WritePsg(12, static_cast<uint8_t>(envelopePeriod >> 8));
	synth.WritePsg(13, static_cast<uint8_t>(shape));

	const double stepSamples = kRate * 16.0 * envelopePeriod / PsgSynth::kClock;
	const int total = static_cast<int>(stepSamples * (steps + 1)) + 16;
	std::vector<int16_t> samples(static_cast<size_t>(total) * 2);
	synth.Render(samples.data(), total);

	std::vector<int16_t> mono(static_cast<size_t>(total));
	for (int frame = 0; frame < total; frame++)
		mono[frame] = samples[frame * 2];

	return LevelSequence(mono, stepSamples, steps);
}


// One of the datasheet's sixteen shapes, as the levels it walks through over
// three cycles. A step either way is allowed: the turning points are one
// sample wide and that is not worth pinning down.
void
CheckShape(PsgSynth& synth, int shape, const char* description,
	std::vector<std::vector<int>> parts)
{
	std::vector<int> expected;
	for (size_t i = 0; i < parts.size(); i++)
		expected.insert(expected.end(), parts[i].begin(), parts[i].end());

	const std::vector<int> got = EnvelopeLevels(synth, shape,
		static_cast<int>(expected.size()));

	int bad = -1;
	for (size_t i = 0; i < expected.size() && bad < 0; i++) {
		if (i >= got.size() || abs(got[i] - expected[i]) > 1)
			bad = static_cast<int>(i);
	}

	gChecks++;
	if (bad < 0) {
		printf("  ok    shape %02x %-38s\n", shape, description);
		return;
	}

	gFailures++;
	printf("  FAIL  shape %02x %-38s step %d: got %d, expected %d\n", shape,
		description, bad, bad < static_cast<int>(got.size()) ? got[bad] : -1,
		expected[bad]);
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

	// The volume ladder is the measured one, not a straight 1.5 dB a step:
	// the quiet end has to be as quiet as the chip made it.
	{
		SingleTone(synth, 0, 254, 15);
		const double full = Peak(Render(synth, kRate / 10));
		SingleTone(synth, 0, 254, 1);
		const double quiet = Peak(Render(synth, kRate / 10));
		Check(fabs(quiet / full - 0.0137) < 0.001, "volume 1 is 0.0137 of full",
			quiet / full, 0.0137);
	}

	// The noise generator: tone off for channel A, noise on.
	{
		synth.Reset();
		synth.WritePsg(7, 0x37);	// tone A/B/C off, noise A on
		synth.WritePsg(6, 1);		// the fastest the noise counter goes
		synth.WritePsg(8, 15);
		std::vector<int16_t> samples = Render(synth, kRate / 10);
		Check(Peak(samples) > 0.0, "noise makes a sound of its own",
			Peak(samples), 1.0);
	}

	// The envelope, attack then hold: the level has to climb and stay up.
	{
		synth.Reset();
		synth.WritePsg(7, 0x3e);	// tone A only
		synth.WritePsg(0, 1);		// a tone far above the sample rate
		synth.WritePsg(1, 0);
		synth.WritePsg(8, 0x10);	// channel A follows the envelope
		// One step is 16 * 256 / clock, a little over a hundred samples, so
		// the rise takes sixteen hundred of them: an envelope period of 1
		// would be over before the first sample.
		synth.WritePsg(11, 0);
		synth.WritePsg(12, 1);
		synth.WritePsg(13, 0x04);	// attack, no continue, hold

		std::vector<int16_t> rise = Render(synth, 1500);
		std::vector<int16_t> held = Render(synth, 512);
		Check(PeakTail(rise, 200) > 8000, "the attack envelope climbs to full",
			PeakTail(rise, 200), 16384);
		Check(PeakTail(held, 128) == 0.0,
			"then, with no continue, drops to silence", PeakTail(held, 128),
			0.0);
	}

	// Shape 0 decays from 15 and holds at nothing.
	{
		synth.Reset();
		synth.WritePsg(7, 0x3e);
		synth.WritePsg(0, 1);
		synth.WritePsg(1, 0);
		synth.WritePsg(8, 0x10);
		synth.WritePsg(11, 0);
		synth.WritePsg(12, 1);
		synth.WritePsg(13, 0x00);	// decay, stop

		std::vector<int16_t> fall = Render(synth, 200);
		std::vector<int16_t> gone = Render(synth, 2000);
		Check(Peak(fall) > 8000.0, "the decay envelope starts loud",
			Peak(fall), 16384);
		Check(PeakTail(gone, 128) == 0.0, "the decay envelope falls to nothing",
			PeakTail(gone, 128), 0.0);
	}

	// Every shape, against the datasheet's own drawing of them. Three cycles
	// is enough to tell a sawtooth from a held ramp from a triangle.
	{
		const std::vector<int> silence = Repeat(0, 32);
		const std::vector<int> full = Repeat(15, 32);
		const std::vector<int> down = Ramp(15, 0);
		const std::vector<int> up = Ramp(0, 15);

		CheckShape(synth, 0x00, "falls and stays silent",
			{ Ramp(15, 0), silence });
		CheckShape(synth, 0x03, "falls and stays silent",
			{ Ramp(15, 0), silence });
		CheckShape(synth, 0x04, "rises then drops to silence",
			{ Ramp(0, 15), silence });
		CheckShape(synth, 0x07, "rises then drops to silence",
			{ Ramp(0, 15), silence });
		CheckShape(synth, 0x08, "repeated falls, a sawtooth",
			{ down, down, down });
		CheckShape(synth, 0x09, "one fall, then silent",
			{ Ramp(15, 0), silence });
		CheckShape(synth, 0x0a, "fall-rise-fall, a triangle",
			{ Ramp(15, 0), Ramp(0, 15), Ramp(15, 0) });
		CheckShape(synth, 0x0b, "falls, then jumps to full and holds",
			{ Ramp(15, 0), full });
		CheckShape(synth, 0x0c, "repeated rises, a sawtooth",
			{ up, up, up });
		CheckShape(synth, 0x0d, "one rise, then holds at full",
			{ Ramp(0, 15), full });
		CheckShape(synth, 0x0e, "rise-fall-rise, a triangle",
			{ Ramp(0, 15), Ramp(15, 0), Ramp(0, 15) });
		CheckShape(synth, 0x0f, "one rise, then silent",
			{ Ramp(0, 15), silence });
	}

	printf("%d of %d checks pass\n", gChecks - gFailures, gChecks);
	return gFailures == 0 ? 0 : 1;
}
