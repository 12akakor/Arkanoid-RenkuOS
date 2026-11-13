/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "SoundPlayer.h"


SoundPlayer::SoundPlayer(State& state, PsgSink* sink)
	:
	fState(state),
	fSink(sink)
{
	for (int reg = 0; reg < 16; reg++)
		fRegs[reg] = 0;
}


uint8_t
SoundPlayer::ReadPsg(uint8_t reg)
{
	if (fSink != NULL)
		return fSink->ReadPsg(reg & 15);
	return fRegs[reg & 15];
}


void
SoundPlayer::SetPsg(uint8_t reg, uint8_t value)
{
	fRegs[reg & 15] = value;
}


void
SoundPlayer::WritePsg(uint8_t reg, uint8_t value)
{
	// The chip has four bits of tone period, five of noise period and four of
	// volume; the top bits of what is written are not remembered.
	static const uint8_t kMask[16] = {
		0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0xff,
		0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f, 0xff, 0xff
	};
	value &= kMask[reg & 15];
	fRegs[reg & 15] = value;
	if (fSink != NULL)
		fSink->WritePsg(reg & 15, value);
}


// PLAY_SOUND (0xB4E8). The code picks a descriptor by arithmetic, not by a
// table of its own: below 128 the index is code + 6 and the stream is the
// first; from 128 to 191 it is code + 144 on the second; from 192 up it is
// (code - 240 + 48) * 2 + 16 on the first and one more than that on the second,
// which is how a musical piece gets two voices.
void
SoundPlayer::Play()
{
	const uint8_t code = fState.Read(kSoundNumber);

	if (code < 128) {
		Queue(kStream1, static_cast<uint8_t>(code + 6));
		return;
	}

	if (code < 192) {
		Queue(kStream2, static_cast<uint8_t>(code + 144));
		return;
	}

	if (code >= 240) {
		Instant(static_cast<uint8_t>(code - 240));
		return;
	}

	const uint8_t index = static_cast<uint8_t>(
		(static_cast<uint8_t>(code - 240) + 48) * 2 + 16);
	Queue(kStream1, index);
	Queue(kStream2, static_cast<uint8_t>(index + 1));
}


// QUEUE_SOUND_DESCRIPTOR (0xB51D). The first byte of the code table is really
// the low byte of a descriptor: B is fixed at 0xB4 and C comes from it, so the
// address is 0xB400 + the byte. The five bytes there are then B0 B1 B2 B3 B4,
// and the stream gets, in this order, active = 1, B2 & 0xF0, B1, B0 & 0x0F,
// B2 & 0x0F, the descriptor's own address, B3, B4 and one tick to wait.
//
// A stream that is already playing is only replaced by a descriptor whose
// priority is not lower.
void
SoundPlayer::Queue(uint16_t stream, uint8_t index)
{
	// The address is 0xB4:index, and the table of pointer bytes begins at
	// 0xB406 - six bytes past 0xB400. That is why PLAY_SOUND adds six to the
	// code before it gets here: the addition cancels this six, and the byte
	// that is actually read is the table's entry for the code itself.
	const uint16_t descriptor = static_cast<uint16_t>(0xb400
		| fState.Read(0xb400 + index));

	if (fState.Read(kCanAddSound) == 0)
		return;

	const bool active = fState.Read(stream + kActive) != 0;
	fState.Write(stream + kActive, 1);

	if (active) {
		const uint8_t priority = fState.Read(descriptor) & 0xf0;
		if (priority < fState.Read(stream + kPriority))
			return;
	}

	fState.Write(stream + kPriority, fState.Read(descriptor + 2) & 0xf0);
	fState.Write(stream + kFollowSoundId, fState.Read(descriptor + 1));
	fState.Write(stream + kChainCount, fState.Read(descriptor) & 0x0f);
	fState.Write(stream + kRepeatCount, fState.Read(descriptor + 2) & 0x0f);

	const uint16_t pointer = static_cast<uint16_t>(descriptor + 2);
	fState.Write(stream + kDescriptorLow, pointer & 0xff);
	fState.Write(stream + kDescriptorHigh, pointer >> 8);

	fState.Write(stream + kSequenceLow, fState.Read(descriptor + 3));
	fState.Write(stream + kSequenceHigh, fState.Read(descriptor + 4));
	fState.Write(stream + kTicks, 1);
}


// The tail of PLAY_SOUND, from 0xB5A2: codes 240 and up are not descriptors but
// a four-bit request, and each set bit does something immediately - put a value
// in the byte after SOUND_NUMBER, raise the voice control mask, or take the
// PSG's reg 7, keep the two bits that are not ours, or in the new value and
// send it. Which of those happens is read straight off the bits of the code.
void
SoundPlayer::Instant(uint8_t value)
{
	// de = SOUND_NUMBER + 1, the byte the request writes into.
	const uint16_t de = kSoundNumber + 1;

	// rrca: bit 0 asks for a return at once.
	bool carry = (value & 1) != 0;
	uint8_t a = static_cast<uint8_t>((value >> 1) | (carry ? 0x80 : 0));
	if (carry)
		return;

	// rra, with no carry coming in.
	carry = (a & 1) != 0;
	a = static_cast<uint8_t>(a >> 1);
	if (carry) {
		fState.Write(de, a);
		fState.Write(kVoiceControl,
			static_cast<uint8_t>(fState.Read(kVoiceControl) | 0x80));
		return;
	}

	// rrca again: bit 2 asks only for the byte after it to be written.
	carry = (a & 1) != 0;
	a = static_cast<uint8_t>((a >> 1) | (carry ? 0x80 : 0));
	if (!carry) {
		fState.Write(de + 1, a);
		return;
	}

	// WRITE_PSG_REG7_PRESERVING_IO_BITS (0xB5A7).
	uint8_t e = 0xbf;
	if ((e & 0x80) == 0)
		return;
	e &= 0x7f;
	fState.Write(de, e);

	WritePsg(7, static_cast<uint8_t>((ReadPsg(7) & 0xc0) | e));
}


// SOUND_ISR_UPDATE (0xB594), the part that writes the registers. The mask says
// which channels may be touched this tick, and it is not one bit per register
// but one bit per channel: 0 and 1 share a period, 2 and 3 share the next, 4 and
// 5 the third, 6 is the noise period, 8, 9 and 10 are the three volumes, and 11
// to 13 are the envelope, which take the last two bits between them. The trace
// this came from is in tools/isr_trace.cpp - the map is measured, not guessed.
void
SoundPlayer::WriteRegisters()
{
	static const struct {
		uint8_t	bit;
		uint8_t	first;
		uint8_t	last;
	} channels[] = {
		{ 0x01, 0, 1 },
		{ 0x02, 2, 3 },
		{ 0x04, 4, 5 },
		{ 0x08, 6, 6 },
		{ 0x10, 8, 8 },
		{ 0x20, 9, 9 },
		{ 0x40, 10, 10 },
		{ 0x80, 11, 13 }
	};

	const uint8_t mask = fState.Read(kRegMask);
	fState.Write(kRegMask, 0);

	for (int channel = 0; channel < 4; channel++) {
		if ((mask & channels[channel].bit) == 0)
			continue;
		for (uint8_t reg = channels[channel].first;
				reg <= channels[channel].last; reg++) {
			WritePsg(reg, fState.Read(kRegsBuffer + reg));
		}
	}

	// The mixer comes back through WRITE_PSG_REG7_PRESERVING_IO_BITS: the two
	// top bits of the chip's own reg 7 are the IO port directions, not the
	// game's, and they are kept.
	uint8_t mixer = fState.Read(kRegsBuffer + 7);
	if ((mixer & 0x80) != 0) {
		mixer &= 0x7f;
		fState.Write(kRegsBuffer + 7, mixer);
		WritePsg(7, static_cast<uint8_t>((ReadPsg(7) & 0xc0) | mixer));
	}

	for (int channel = 4; channel < 8; channel++) {
		if ((mask & channels[channel].bit) == 0)
			continue;
		for (uint8_t reg = channels[channel].first;
				reg <= channels[channel].last; reg++) {
			WritePsg(reg, fState.Read(kRegsBuffer + reg));
		}
	}

	// The byte past the image carries one more request: bit 3 sends reg 13
	// once more, and is cleared so that it is sent once.
	const uint8_t flags = fState.Read(kFlags);
	if ((flags & 0x08) != 0) {
		WritePsg(13, fState.Read(kRegsBuffer + 13));
		fState.Write(kFlags, static_cast<uint8_t>(flags & ~0x08));
	}
}


// SOUND_ISR_UPDATE (0xB594). A tick called with sound inhibited does nothing at
// all, which is what the game does between rounds.
void
SoundPlayer::Tick()
{
	if (fState.Read(kInhibit) != 0)
		return;

	WriteRegisters();
}


// ADVANCE_SOUND_STREAM_IF_READY (0xB47A).
//
// Four bytes of the stream are a little ladder: the follow-sound id at +2, the
// chain count at +3 and the repeat count at +4. Each tick decrements the
// countdown at +9; while it is not zero nothing else happens. When it reaches
// zero the byte the stream pointer aims at is looked at - if it is not zero
// there is a command, and the carry says so. If it is zero the ladder runs:
// the repeat count is spent first, reloading the stream from its own descriptor;
// then the chain count, which steps to the descriptor that follows it in
// memory; and when both are spent the stream goes inactive and whatever follow
// sound it was carrying is played in its place.
bool
SoundPlayer::Advance(uint16_t stream, uint16_t pointer)
{
	if (fState.Read(stream + kActive) == 0)
		return false;

	fState.Write(stream + kTicks,
		static_cast<uint8_t>(fState.Read(stream + kTicks) - 1));
	if (fState.Read(stream + kTicks) != 0)
		return false;

	const uint8_t command = fState.Read(pointer);
	if (command != 0)
		return true;

	const uint8_t repeat = static_cast<uint8_t>(
		fState.Read(stream + kRepeatCount) - 1);
	fState.Write(stream + kRepeatCount, repeat);

	if (repeat != 0) {
		_Reload(stream, static_cast<uint16_t>(
			fState.Read(stream + kDescriptorLow)
			| (fState.Read(stream + kDescriptorHigh) << 8)));
		return false;
	}

	const uint8_t chain = static_cast<uint8_t>(
		fState.Read(stream + kChainCount) - 1);
	fState.Write(stream + kChainCount, chain);

	const uint16_t descriptor = static_cast<uint16_t>(
		fState.Read(stream + kDescriptorLow)
		| (fState.Read(stream + kDescriptorHigh) << 8));

	if (chain != 0) {
		// The descriptor that follows this one in memory: five bytes on, with
		// its own repeat nibble and sequence pointer.
		const uint16_t next = static_cast<uint16_t>(descriptor + 3);
		fState.Write(stream + kRepeatCount, fState.Read(next) & 0x0f);
		fState.Write(stream + kDescriptorLow, next & 0xff);
		fState.Write(stream + kDescriptorHigh, next >> 8);
		_Reload(stream, next);
		return false;
	}

	// Both counts spent: the stream ends and the follow sound takes over.
	fState.Write(stream + kActive, 0);
	fState.Write(kSoundNumber, fState.Read(stream + kFollowSoundId));
	Play();
	return false;
}


// The shared tail of the reload paths, the `jr lb54dh` into the middle of
// QUEUE_SOUND_DESCRIPTOR: the sequence pointer comes from descriptor + 1 and
// the countdown is rearmed to one tick.
void
SoundPlayer::_Reload(uint16_t stream, uint16_t descriptor)
{
	const uint16_t sequence = static_cast<uint16_t>(descriptor + 1);
	fState.Write(stream + kSequenceLow, fState.Read(sequence));
	fState.Write(stream + kSequenceHigh, fState.Read(sequence + 1));
	fState.Write(stream + kTicks, 1);
}
