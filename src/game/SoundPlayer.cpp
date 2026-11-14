/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


#include "SoundPlayer.h"

#include <stdlib.h>


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
// all, which is what the game does between rounds. Otherwise: the register
// image goes out, both streams get a chance to advance and run their commands,
// and then the three updaters walk the ramps that the commands set up.
void
SoundPlayer::Tick()
{
	if (fState.Read(kInhibit) != 0)
		return;

	WriteRegisters();

	for (int index = 0; index < 2; index++) {
		const uint16_t stream = index == 0 ? kStream1 : kStream2;
		const uint16_t pointerAddress = index == 0 ? kPointer1 : kPointer2;
		const uint16_t pointer = Read16(pointerAddress);

		if (!Advance(stream, pointer))
			continue;

		// The byte the stream stopped on is the wait for the next tick.
		fState.Write(stream + kTicks, fState.Read(pointer));

		// The two dispatchers load each other's table: DISPATCH_PRIMARY
		// (0xB63E) uses TBL_SECONDARY_SOUND_CMD_ENTRY_OFFSETS and
		// DISPATCH_SECONDARY (0xB649) uses the primary one. The first stream
		// therefore runs the commands the tables call secondary.
		Write16(pointerAddress, Dispatch(stream, pointer, index != 0));
	}

	uint16_t accumulator = Read16(kRegsBuffer);
	accumulator = _UpdatePeriod(static_cast<uint16_t>(kStream1 + 11), 0x01,
		accumulator);
	Write16(kRegsBuffer, accumulator);

	uint8_t volume = fState.Read(kVolumeControl);
	volume = _UpdateVolume(static_cast<uint16_t>(kStream1 + 14), 0x10, volume);
	volume = _UpdateDelay(0x10, volume);
	fState.Write(kVolumeControl, volume);

	accumulator = Read16(kPeriodControl);
	accumulator = _UpdatePeriod(static_cast<uint16_t>(kStream2 + 11), 0x02,
		accumulator);
	Write16(kPeriodControl, accumulator);

	volume = fState.Read(kVolume);
	volume = _UpdateVolume(static_cast<uint16_t>(kStream2 + 14), 0x40, volume);
	fState.Write(kVolume, volume);
}


uint16_t
SoundPlayer::Read16(uint16_t address)
{
	return static_cast<uint16_t>(fState.Read(address)
		| (fState.Read(address + 1) << 8));
}


void
SoundPlayer::Write16(uint16_t address, uint16_t value)
{
	fState.Write(address, static_cast<uint8_t>(value & 0xff));
	fState.Write(address + 1, static_cast<uint8_t>(value >> 8));
}


// UPDATE_PERIOD_EFFECT (0xB77B): a period ramp. The state's first byte holds
// the flags, the second the tick countdown and the third the delta; the delta
// is taken as signed and added to the period the register image holds.
uint16_t
SoundPlayer::_UpdatePeriod(uint16_t state, uint8_t d, uint16_t accumulator)
{
	const uint8_t flags = fState.Read(state);
	if ((flags & 0x80) == 0)
		return accumulator;

	++state;
	uint8_t ticks = static_cast<uint8_t>(fState.Read(state) - 1);
	fState.Write(state, ticks);
	if (ticks != 0)
		return accumulator;

	fState.Write(state, static_cast<uint8_t>(flags & 0x07));

	++state;
	uint8_t delta = fState.Read(state);
	if ((flags & 0x40) != 0) {
		delta = static_cast<uint8_t>(-delta);
		fState.Write(state, delta);
	}

	accumulator = static_cast<uint16_t>(accumulator
		+ static_cast<int16_t>(static_cast<int8_t>(delta)));
	_Mask(d);
	return accumulator;
}


// UPDATE_VOLUME_EFFECT (0xB79B): a volume ramp, two steps at a time upward or
// one downward, and when its count runs out it walks to the next step of its
// preset block.
uint8_t
SoundPlayer::_UpdateVolume(uint16_t state, uint8_t d, uint8_t a)
{
	uint8_t c = a;
	uint8_t flags = fState.Read(state);
	if ((flags & 0x80) == 0)
		return c;

	++state;
	uint8_t ticks = static_cast<uint8_t>(fState.Read(state) - 1);
	fState.Write(state, ticks);
	if (ticks != 0)
		return c;

	fState.Write(state, static_cast<uint8_t>(flags & 0x0f));

	++state;
	uint8_t count = static_cast<uint8_t>(fState.Read(state) - 1);
	fState.Write(state, count);

	if (count == 0) {
		++state;
		++state;
		const uint8_t sum = static_cast<uint8_t>(
			fState.Read(state) + 0xf0);
		fState.Write(state, sum);
		--state;

		if (sum != 0) {
			_Step(state);
		} else {
			fState.Write(static_cast<uint16_t>(state - 2), 0);
		}
		return c;
	}

	if ((flags & 0x40) != 0) {
		c = static_cast<uint8_t>(c + 2);
		if (c > 15) {
			c = 15;
			return c;
		}
		_Mask(d);
		return c;
	}

	if (c == 0)
		return c;
	c = static_cast<uint8_t>(c - 1);
	_Mask(d);
	return c;
}


// UPDATE_DELAYED_REPEAT_EFFECT (0xB7CF): the delay that keeps a note repeating
// while its count lasts.
uint8_t
SoundPlayer::_UpdateDelay(uint8_t d, uint8_t c)
{
	if (c == 0)
		return c;

	uint16_t hl = kDelayStatus;
	const uint8_t status = fState.Read(hl);
	if ((status & 0x80) == 0)
		return c;

	++hl;
	uint8_t ticks = static_cast<uint8_t>(fState.Read(hl) - 1);
	fState.Write(hl, ticks);
	if (ticks != 0)
		return c;

	const uint8_t value = static_cast<uint8_t>(
		fState.Read(kDelayStatus) & 0x1f);
	fState.Write(kDelayStateC, value);

	c = static_cast<uint8_t>(c - 1);
	if (c == 0)
		fState.Write(kDelayStatus, value);

	_Mask(d);
	return c;
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


// ---------------------------------------------------------------------------
// The command handlers, 0xB676.


void
SoundPlayer::_Mask(uint8_t d)
{
	fState.Write(kRegMask, static_cast<uint8_t>(fState.Read(kRegMask) | d));
}


void
SoundPlayer::_Volume(uint8_t d, uint8_t value)
{
	// The mask bit that says which channel this volume belongs to also says
	// which of the three bytes holds it: 0x10 is the first stream's block,
	// 0x20 the second stream's mixer byte, 0x40 the second stream's own
	// volume. Writing them all to the first byte left the second stream's
	// notes reading the first stream's volume.
	const uint16_t address = d == 0x10 ? kVolumeControl
		: (d == 0x20 ? kVolumeControl2 : kVolume);
	fState.Write(address, value);
	_Mask(d);
}


// CMD_SET_ONE_NOTE_ON_CHANNEL (0xB6E6) and the shared tail at 0xB700.
uint16_t
SoundPlayer::_Note(uint16_t stream, uint16_t bc, uint16_t hl, uint8_t d,
	uint8_t e)
{
	uint8_t a = static_cast<uint8_t>(fState.Read(stream + 11) & 0x07);
	fState.Write(stream + 12, a);

	if ((fState.Read(stream + 11) & 0x40) != 0) {
		const uint8_t delta = fState.Read(stream + 13);
		if ((delta & 0x80) != 0)
			fState.Write(stream + 13, static_cast<uint8_t>(-delta));
	}

	fState.Write(hl, e);
	++bc;
	a = fState.Read(bc);
	--hl;
	fState.Write(hl, a);

	// 0xB705: the bit goes up only when the channel's bit is already in the
	// flags, which is the opposite of how it reads at a glance.
	if ((fState.Read(kFlags) & d) != 0)
		fState.Write(kFlags, static_cast<uint8_t>(fState.Read(kFlags) | 0x08));

	_Mask(d);
	return bc;
}


// The effect-state entry at 0xB6CD: start or continue a period or volume ramp.
uint16_t
SoundPlayer::_Effect(uint16_t stream, uint16_t bc, uint16_t hl, uint8_t a,
	uint8_t e)
{
	(void)stream;
	fState.Write(hl, a);
	if (a == 0)
		return bc;

	fState.Write(hl, static_cast<uint8_t>(a | 0x80));
	++bc;

	const uint8_t byte = fState.Read(bc);
	const uint8_t rotated = static_cast<uint8_t>((byte << 1) | (byte >> 7));
	const bool carry = (rotated & 0x01) != 0;

	uint8_t value = rotated;
	value = static_cast<uint8_t>((value & 0x80) | (value >> 1));
	if (!carry) {
		value = byte;
		fState.Write(hl, static_cast<uint8_t>(fState.Read(hl) | 0x40));
	}

	fState.Write(hl + 1, e);
	fState.Write(hl + 2, value);
	return bc;
}


// LOAD_NEXT_EFFECT_PRESET_STEP (0xB807). HL points at the byte that counts
// through a preset block; the three bytes below it are the tick period, the
// level and the level with its two flag bits.
uint8_t
SoundPlayer::_Step(uint16_t& hl)
{
	fState.Write(hl, static_cast<uint8_t>(fState.Read(hl) + 1));
	const uint8_t step = fState.Read(static_cast<uint16_t>(0xb400
		| fState.Read(hl)));

	const uint8_t flags = static_cast<uint8_t>(0x80 | ((step & 0xc0) >> 1));
	const bool plain = flags == 0xe0;

	--hl;
	fState.Write(hl, 1);
	if (!plain)
		fState.Write(hl, static_cast<uint8_t>(((step & 0x78) >> 3) + 1));

	uint8_t level = plain
		? static_cast<uint8_t>(step & 0x3f)
		: static_cast<uint8_t>(step & 0x07);
	level = static_cast<uint8_t>((level & 0x3f) + 1);

	--hl;
	fState.Write(hl, level);
	--hl;
	fState.Write(hl, static_cast<uint8_t>(level | flags));

	// The byte the step loader leaves in E: the command uses it as the volume.
	return step;
}


// INIT_EFFECT_FROM_PRESET (0xB7E6). The preset index is the byte at 'state';
// the table at 0xB4AE turns it into the low byte of a block. The two bytes
// above the state take the block's flags and its address, and the three below
// take the first step. The value returned is the volume the command puts in
// the register image: the block's low nibble when the block opens with bit 7
// set, the first step's own byte when it does not.
bool
SoundPlayer::_InitEffect(uint16_t state, uint8_t& volume)
{
	// A preset of zero is not "no effect yet": the routine returns two levels
	// up, so the command that called it stops there and the volume the stream
	// already has is left alone. That is why a command early in a sequence
	// keeps its volume instead of being silenced by the next one.
	const uint8_t preset = fState.Read(state);
	if (preset == 0)
		return false;

	const uint16_t block = static_cast<uint16_t>(0xb400
		| fState.Read(0xb4ae + preset));
	const uint8_t header = fState.Read(block);

	fState.Write(state - 1, static_cast<uint8_t>(header & 0x70));
	fState.Write(state - 2, static_cast<uint8_t>(block & 0xff));

	uint16_t hl = static_cast<uint16_t>(state - 2);
	const uint8_t step = _Step(hl);

	volume = (header & 0x80) != 0
		? static_cast<uint8_t>(header & 0x0f)
		: step;
	return true;
}


// UPDATE_MIXER_FROM_CHANNEL_MASK (0xB82D): clears the stream's channel byte and
// folds the two new channel bits into the voice control register.
//
// It returns true when the handler must abandon the rest of its body. The
// routine ends `ld e,0x10 / srl h / ret c / pop af / ret`: with the carry set it
// goes back to the handler, and with it clear the `pop af` throws away the
// handler's return address so the return goes to the dispatcher instead. That
// is not a trick to read past - it decides whether the command sets its mask.
bool
SoundPlayer::_MixerMask(uint16_t stream, uint8_t d, uint8_t& e, uint16_t bc,
	bool clearChannel)
{
	if (clearChannel)
		fState.Write(stream + 11, 0);

	uint8_t l = fState.Read(kVoiceControl);
	const uint8_t command = fState.Read(bc);

	bool carry = (command & 0x01) != 0;
	uint8_t h = static_cast<uint8_t>((command >> 1) | (carry ? 0x80 : 0));

	// The two branches are not the same operation: with the carry set the
	// channel is turned off, `and l` then replaces L outright; with it clear
	// the value is merely OR'd in. Doing both as an OR leaves the bit on.
	uint8_t a = carry ? static_cast<uint8_t>(~e & l)
		: static_cast<uint8_t>(e | l);
	l = a;

	carry = (h & 0x01) != 0;
	h = static_cast<uint8_t>(h >> 1);

	a = carry ? static_cast<uint8_t>(~d & l) : static_cast<uint8_t>(d | l);
	l = a;

	fState.Write(kVoiceControl, static_cast<uint8_t>(l | 0x80));

	e = 0x10;
	const bool carry3 = (h & 0x01) != 0;
	return !carry3;
}


// One command byte. The high nibble picks the handler through the table the
// dispatcher chose, the low nibble is its parameter, and BC walks forward
// through the sequence.
uint16_t
SoundPlayer::_Command(uint16_t stream, uint16_t bc, uint8_t command,
	bool primaryTable)
{
	const uint8_t index = static_cast<uint8_t>((command >> 4) & 0x07);
	uint8_t e = static_cast<uint8_t>(command & 0x0f);
	uint16_t state = static_cast<uint16_t>(stream + 11);	// period effect

	if (primaryTable) {
		switch (index) {
			case 0:
				return _Note(stream, bc, kPeriodControlHi, 0x02, e);

			case 1:
				fState.Write(kEnvelopeShape, e);
				_Mask(0x20);
				return bc;

			case 2:
				if ((e & 0x08) != 0) {
					// ld de,0x1002: D is the channel bit, E the new mixer
					// value, and E is what the envelope write below uses.
					uint8_t mixer = 0x02;
					if (_MixerMask(stream, 0x10, mixer, bc, true))
						return bc;
					fState.Write(kEnvelopeShape, mixer);
					_Mask(0x20);
					return bc;
				}
				return _Effect(stream, bc, state, e, e);

			case 3: {
				if ((e & 0x08) == 0) {
					fState.Write(kFlags, static_cast<uint8_t>(
						(e << 1) | (e >> 7)));
					return bc;
				}
				fState.Write(kComplexFlags, e);
				++bc;
				uint8_t a = fState.Read(bc);
				fState.Write(kComplexA, a);
				++bc;
				a = fState.Read(bc);
				fState.Write(kComplexB, a);
				_Mask(0x80);
				return bc;
			}

			case 4: {
				uint8_t a = e;
				for (int i = 0; i < 4; i++)
					a = static_cast<uint8_t>((a << 1) | (a >> 7));
				fState.Write(kComplexA, a);
				++bc;
				a = fState.Read(bc);
				fState.Write(kComplexB, a);
				_Mask(0x80);
				return bc;
			}

			case 5: {
				bc = _Note(stream, bc, kAuxPeriodHi, 0x04, e);
				uint8_t volume;
				if (!_InitEffect(0xe5fc, volume))
					return bc;
				_Volume(0x40, volume);
				return bc;
			}

			case 6:
				_Volume(0x40, e);
				return bc;

			default: {
				if ((e & 0x08) != 0) {
					// ld de,0x2004.
					uint8_t mixer = 0x04;
					if (_MixerMask(stream, 0x20, mixer, bc, false))
						return bc;
					fState.Write(0xe5fc, 0);
					fState.Write(0xe5f7, 0);
					_Volume(0x40, mixer);
					return bc;
				}
				fState.Write(0xe5fc, e);
				if (e != 0) {
					uint8_t volume;
					if (!_InitEffect(0xe5fc, volume))
						return bc;
					_Volume(0x40, volume);
				} else {
					fState.Write(0xe5f7, 0);
				}
				return bc;
			}
		}
	}

	switch (index) {
		case 0: {
			bc = _Note(stream, bc, 0xe5c5, 0x01, e);
			uint8_t volume;
			if (!_InitEffect(0xe5e6, volume))
				return bc;
			uint8_t status = static_cast<uint8_t>(
				fState.Read(kDelayStatus) & 0x1f);
			if (status != 0) {
				fState.Write(kDelayStatus, static_cast<uint8_t>(
					fState.Read(kDelayStatus) | 0x80));
				fState.Write(kDelayStateC, status);
			}
			_Volume(0x10, volume);
			return bc;
		}

		case 1: {
			uint8_t status = static_cast<uint8_t>(
				fState.Read(kDelayStatus) & 0x1f);
			if (status != 0) {
				fState.Write(kDelayStatus, static_cast<uint8_t>(
					fState.Read(kDelayStatus) | 0x80));
				fState.Write(kDelayStateC, status);
			}
			_Volume(0x10, e);
			return bc;
		}

		case 2:
			if ((e & 0x08) != 0) {
				// ld de,0x0801.
				uint8_t mixer = 0x01;
				if (_MixerMask(stream, 0x08, mixer, bc, true))
					return bc;
				fState.Write(kDelayStatus, 0);
				fState.Write(0xe5e6, 0);
				fState.Write(0xe5e1, 0);
				_Volume(0x10, mixer);
				return bc;
			}
			return _Effect(stream, bc, state, e, e);

		case 3:
			fState.Write(0xe5e6, e);
			if (e == 0) {
				fState.Write(0xe5e1, 0);
				return bc;
			}
			{
				uint8_t volume;
				if (!_InitEffect(0xe5e6, volume))
					return bc;
				uint8_t status = static_cast<uint8_t>(
					fState.Read(kDelayStatus) & 0x1f);
				if (status != 0) {
					fState.Write(kDelayStatus, static_cast<uint8_t>(
						fState.Read(kDelayStatus) | 0x80));
					fState.Write(kDelayStateC, status);
				}
				_Volume(0x10, volume);
			}
			return bc;

		case 4: {
			if (e == 0) {
				fState.Write(kDelayStatus, 0);
				return bc;
			}
			fState.Write(kDelayStateC, e);
			fState.Write(kDelayStatus, static_cast<uint8_t>(e | 0x80));
			return bc;
		}

		case 5: {
			const uint8_t a = static_cast<uint8_t>(e | 0x10);
			fState.Write(kDelayStateC, a);
			fState.Write(kDelayStatus, static_cast<uint8_t>(a | 0x80));
			return bc;
		}

		case 6:
			fState.Write(kNoise, e);
			_Mask(0x08);
			return bc;

		default: {
			const uint8_t a = static_cast<uint8_t>(e | 0x10);
			fState.Write(kNoise, a);
			_Mask(0x08);
			return bc;
		}
	}
}


// The dispatcher: read command bytes until one has bit 7 clear, which is the
// wait count for the next tick, and leave the stream pointer on it.
uint16_t
SoundPlayer::Dispatch(uint16_t stream, uint16_t pointer, bool primary)
{
	for (;;) {
		++pointer;
		const uint8_t command = fState.Read(pointer);
		if ((command & 0x80) == 0)
			return pointer;
		pointer = _Command(stream, pointer, command, primary);
	}
}
