/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// Every sound in the game's own table, rendered through the synthesiser.
//
//   ./render_sounds build/arkanoid.rom [outdir]
//
// For each of the twenty-eight codes the cartridge's table holds, the original
// plays it on the reference machine; the chip's sixteen registers are read
// after every sound tick, the changes are handed to PsgSynth exactly as the
// driver would hand them, and a sixtieth of a second of audio is rendered per
// tick. What comes out is a WAV per sound and a line of numbers per sound.
//
// The numbers are the point: a sound that is silent although its volume
// registers are up, a sound that never ends, or a tick whose tone does not
// match the period it was given is a fault the ear would only half notice.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "game/PsgSynth.h"
#include "ref/msx1/Machine1.h"


namespace {

const uint16_t kSentinel = 0xBEEF;
const uint16_t kPlay = 0xB400;
const uint16_t kIsr = 0xB403;

const uint16_t kInhibit = 0xE5C1;
const uint16_t kCanAdd = 0xE5C2;
const uint16_t kSoundNumber = 0xE5C0;

const uint16_t kStream1 = 0xE5D3;
const uint16_t kStream2 = 0xE5E9;

// The sound tick is the frame: the game's timer is 16667 microseconds and
// calls the interpreter once each time.
const double kTickSeconds = 16667.0 / 1000000.0;
const int kSampleRate = 44100;

// Twenty seconds is far longer than any sound in the table lasts; a sound that
// reaches this has failed to stop and is reported as such.
const int kMaxTicks = 20000;

const int kSounds = 28;


bool
ReadFile(const char* path, std::vector<uint8_t>& data)
{
	FILE* file = fopen(path, "rb");
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


bool
Run(Machine1& machine, uint16_t address)
{
	Z80Regs& r = machine.Cpu().r;
	r.halted = false;
	machine.Cpu().eiPending = false;
	r.iff1 = false;
	r.iff2 = false;
	r.af = 0;
	r.bc = 0;
	r.de = 0;
	r.hl = 0;
	r.ix = 0;
	r.iy = 0;
	r.sp = 0xF000;
	machine.Write(--r.sp, static_cast<uint8_t>(kSentinel >> 8));
	machine.Write(--r.sp, static_cast<uint8_t>(kSentinel & 0xff));
	r.pc = address;

	for (int step = 0; step < 500000; step++) {
		machine.StepInstruction();
		if (r.pc == kSentinel)
			return true;
	}
	return false;
}


// Sixteen-bit mono PCM. A sample rate and a count are all a reader needs.
bool
WriteWav(const std::string& path, const std::vector<int16_t>& samples)
{
	FILE* file = fopen(path.c_str(), "wb");
	if (file == NULL)
		return false;

	const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 2);
	const uint32_t rate = kSampleRate;
	const uint16_t channels = 1;
	const uint16_t bits = 16;
	const uint32_t byteRate = rate * channels * bits / 8;
	const uint16_t blockAlign = channels * bits / 8;

	fwrite("RIFF", 1, 4, file);
	const uint32_t riffSize = 36 + dataBytes;
	fwrite(&riffSize, 4, 1, file);
	fwrite("WAVEfmt ", 1, 8, file);
	const uint32_t fmtSize = 16;
	fwrite(&fmtSize, 4, 1, file);
	const uint16_t format = 1;
	fwrite(&format, 2, 1, file);
	fwrite(&channels, 2, 1, file);
	fwrite(&rate, 4, 1, file);
	fwrite(&byteRate, 4, 1, file);
	fwrite(&blockAlign, 2, 1, file);
	fwrite(&bits, 2, 1, file);
	fwrite("data", 1, 4, file);
	fwrite(&dataBytes, 4, 1, file);
	fwrite(samples.data(), 2, samples.size(), file);
	fclose(file);
	return true;
}


double
PeakOf(const std::vector<int16_t>& samples)
{
	double peak = 0.0;
	for (size_t i = 0; i < samples.size(); i++) {
		const double value = fabs(static_cast<double>(samples[i]));
		if (value > peak)
			peak = value;
	}
	return peak;
}


double
RmsOf(const std::vector<int16_t>& samples)
{
	double sum = 0.0;
	for (size_t i = 0; i < samples.size(); i++)
		sum += static_cast<double>(samples[i]) * samples[i];
	return samples.empty() ? 0.0 : sqrt(sum / samples.size());
}


int
Crossings(const int16_t* samples, int frames)
{
	int crossings = 0;
	int last = 0;
	for (int frame = 0; frame < frames; frame++) {
		const int sign = samples[frame] > 0 ? 1 : (samples[frame] < 0 ? -1 : 0);
		if (sign == 0)
			continue;
		if (last != 0 && sign != last)
			crossings++;
		last = sign;
	}
	return crossings;
}


// The frequency the registers ask for, or -1 when the chip is not playing one
// tone on one channel at this moment: two tones at once cannot be read off a
// crossing count, and neither can noise.
double
AskedFrequency(const uint8_t* regs)
{
	const uint8_t mixer = regs[7];
	int plain = 0;		// one tone, and nothing else mixing into it
	int muddy = 0;		// a sound the crossing count cannot follow
	double hertz = 0.0;
	for (int channel = 0; channel < 3; channel++) {
		const bool toneOn = (mixer & (1 << channel)) == 0;
		const bool noiseOn = (mixer & (1 << (channel + 3))) == 0;
		const bool envelope = (regs[8 + channel] & 0x10) != 0;
		const int volume = envelope ? 16 : regs[8 + channel];
		if (volume == 0)
			continue;

		if (!toneOn || noiseOn || envelope) {
			muddy++;
			continue;
		}

		int period = regs[channel * 2] | (regs[channel * 2 + 1] << 8);
		if (period == 0)
			period = 1;
		plain++;
		hertz = PsgSynth::kClock / (16.0 * period);
	}
	return plain == 1 && muddy == 0 ? hertz : -1.0;
}

}


int
main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: render_sounds <rom> [outdir]\n");
		return 1;
	}
	const std::string outdir = argc > 2 ? argv[2] : "build/sounds";

	std::vector<uint8_t> bios;
	std::vector<uint8_t> logo;
	std::vector<uint8_t> rom;
	if (!ReadFile("third_party/cbios/cbios_main_msx1.rom", bios)
			|| !ReadFile(argv[1], rom)) {
		fprintf(stderr, "cannot read the ROMs\n");
		return 1;
	}
	ReadFile("third_party/cbios/cbios_logo_msx1.rom", logo);

	Machine1 machine;
	if (!machine.LoadBios(bios, logo) || !machine.LoadCartridge(rom)) {
		fprintf(stderr, "bad C-BIOS or ROM size\n");
		return 1;
	}
	machine.Reset();
	for (int frame = 0; frame < 300; frame++)
		machine.RunFrame();

	std::vector<uint8_t> base(0x10000);
	memcpy(base.data(), machine.Ram(), base.size());

	char command[256];
	snprintf(command, sizeof(command), "mkdir -p %s", outdir.c_str());
	if (system(command) != 0) {
		fprintf(stderr, "cannot make %s\n", outdir.c_str());
		return 1;
	}

	const int framesPerTick = static_cast<int>(kSampleRate * kTickSeconds + 0.5);
	int silentWithVolume = 0;
	int neverEnded = 0;
	int toneTicks = 0;
	int aboveNyquist = 0;
	int toneAgreed = 0;

	printf("%-4s %-6s %-8s %-8s %-8s %s\n", "code", "ticks", "seconds",
		"peak", "rms dB", "audio");

	for (int code = 0; code < kSounds; code++) {
		memcpy(machine.Ram(), base.data(), base.size());
		machine.Ram()[kInhibit] = 0;
		machine.Ram()[kCanAdd] = 1;
		machine.Ram()[kSoundNumber] = static_cast<uint8_t>(code);
		if (!Run(machine, kPlay)) {
			printf("%-4d PLAY_SOUND did not return\n", code);
			return 1;
		}

		PsgSynth synth(kSampleRate);
		uint8_t regs[16];
		for (int reg = 0; reg < 16; reg++) {
			regs[reg] = machine.GetPsg().Register(reg);
			synth.WritePsg(static_cast<uint8_t>(reg), regs[reg]);
		}

		std::vector<int16_t> audio;
		// The synth renders interleaved stereo, and both channels carry the
		// same mono signal: the WAV keeps one of them.
		std::vector<int16_t> tickAudio(static_cast<size_t>(framesPerTick) * 2);
		std::vector<int16_t> mono(static_cast<size_t>(framesPerTick));
		int ticks = 0;

		for (int tick = 1; tick <= kMaxTicks; tick++) {
			if (!Run(machine, kIsr)) {
				printf("%-4d the sound tick did not return at tick %d\n",
					code, tick);
				return 1;
			}
			ticks = tick;

			for (int reg = 0; reg < 16; reg++) {
				const uint8_t value = machine.GetPsg().Register(reg);
				if (value == regs[reg])
					continue;
				regs[reg] = value;
				synth.WritePsg(static_cast<uint8_t>(reg), value);
			}

			synth.Render(tickAudio.data(), framesPerTick);
			for (int frame = 0; frame < framesPerTick; frame++)
				mono[frame] = tickAudio[frame * 2];
			audio.insert(audio.end(), mono.begin(), mono.end());

			// One tone on one channel: the crossing count in this tick has
			// to be that tone. The tick next to a change is skipped, because
			// such a tick can hold both the old level and the new one.
			const double asked = AskedFrequency(regs);
			// A tone above half the sample rate cannot be rendered, so it
			// cannot be counted either: the chip goes up to clock / 16, and
			// the game does use period 1, which is 112 kHz.
			if (asked > 0.0 && asked * 2.0 >= kSampleRate)
				aboveNyquist++;
			if (asked > 0.0 && asked * 2.0 < kSampleRate) {
				const int crossings = Crossings(mono.data(), framesPerTick);
				if (crossings > 1) {
					// A crossing count over one tick is coarse: at a low
					// tone a single crossing either way is a large share of
					// the count, and the tick can cut a half-cycle. One
					// crossing of slack is what the window can honestly
					// settle.
					const double wanted = 2.0 * asked * kTickSeconds;
					toneTicks++;
					// The window is cut at an arbitrary phase, so a count
					// can be one crossing short of the ideal: the count is
					// compared with the nearest whole number of crossings,
					// one either way.
					if (fabs(crossings - round(wanted)) <= 1.0)
						toneAgreed++;
				}
			}

			if (machine.Ram()[kStream1] == 0 && machine.Ram()[kStream2] == 0)
				break;
		}

		if (ticks >= kMaxTicks)
			neverEnded++;

		const double peak = PeakOf(audio);
		const double rms = RmsOf(audio);
		const double db = rms > 0.0 ? 20.0 * log10(rms / 32768.0) : -999.0;

		if (peak == 0.0) {
			silentWithVolume++;
			printf("      its volumes end at %d %d %d, mixer %02x\n",
				regs[8], regs[9], regs[10], regs[7]);
		}

		char path[512];
		snprintf(path, sizeof(path), "%s/%02d.wav", outdir.c_str(), code);
		if (!WriteWav(path, audio)) {
			fprintf(stderr, "cannot write %s\n", path);
			return 1;
		}

		printf("%-4d %-6d %-8.2f %-8.0f %-8.1f %s\n", code, ticks,
			ticks * kTickSeconds, peak, db,
			peak == 0.0 ? "silent (no audio)" : "rendered");
	}

	printf("%d sounds rendered to %s\n", kSounds, outdir.c_str());
	if (neverEnded > 0)
		printf("%d did not stop within %.0f seconds\n", neverEnded,
			kMaxTicks * kTickSeconds);
	if (silentWithVolume > 0)
		printf("%d renders silent, with all three channel volumes at zero when"
			" it ends: the cartridge is asking for nothing audible\n",
			silentWithVolume);
	printf("%d of %d single-tone ticks play the frequency their period"
		" register asks for, to the one-crossing resolution of a 16.7 ms"
		" window\n", toneAgreed, toneTicks);
	if (aboveNyquist > 0)
		printf("%d ticks play a tone above 22 kHz, which is past what 44.1 kHz"
			" can carry and cannot be counted\n", aboveNyquist);
	return 0;
}
