/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The sound player, front half: taking a code and setting a stream up.
//
// The game does not tell the PSG anything. It puts a code in SOUND_NUMBER and
// calls PLAY_SOUND; that picks one of 28 five-byte descriptors - or two of them,
// for the musical pieces - and hands each to QUEUE_SOUND_DESCRIPTOR, which
// decides whether the stream it belongs to may be interrupted and, if it may,
// writes the stream's six bytes of state and the sequence pointer.
//
// The stream state is the six bytes at SOUND_BUFFER_1 (0xE5D3) and
// SOUND_BUFFER_2 (0xE5E9):
//
//   +0  active                      +5  descriptor pointer, low
//   +1  priority                    +6  descriptor pointer, high
//   +2  follow sound id             +7  sequence pointer, low
//   +3  chain count                 +8  sequence pointer, high
//   +4  repeat count                +9  ticks until the next command
//
// What is *not* here yet is the interpreter that walks the sequence and writes
// the PSG - SOUND_ISR_UPDATE and its command handlers. This half is what the
// game calls; the other half is what makes a noise.


#ifndef ARKANOID_SOUNDPLAYER_H
#define ARKANOID_SOUNDPLAYER_H


#include <stddef.h>
#include <stdint.h>

#include "State.h"


class SoundPlayer {
public:
	// Where the player's register writes go. The reference machine has a real
	// PSG; the game on this OS has whatever the platform gives it, which is
	// not this class's business.
	class PsgSink {
	public:
		virtual				~PsgSink() {}
		virtual	void		WritePsg(uint8_t reg, uint8_t value) = 0;
		virtual	uint8_t		ReadPsg(uint8_t reg) = 0;
	};

	enum {
		kStream1			= 0xE5D3,	// SOUND_BUFFER_1
		kStream2			= 0xE5E9,	// SOUND_BUFFER_2
		kStreamLength		= 0x16,
		kSoundNumber		= 0xE5C0,	// SOUND_NUMBER
		kCanAddSound		= 0xE5C2,	// CAN_ADD_SOUND
		kSoundParams		= 0xB406,	// TBL_SOUND_PARAMS
		kVoiceControl		= 0xE5CB,	// SOUND_VOICE_CONTROL
		kInhibit			= 0xE5C1,	// SOUND_INHIBIT
		kRegMask			= 0xE5C3,	// SOUND_REG_MASK
		kRegsBuffer			= 0xE5C4,	// SOUNDS_REGS_BUFFER, regs 0..13
		kFlags				= 0xE5D2,	// SOUND_PARALLEL, bit 3 re-sends reg 13

		kActive				= 0,
		kPriority			= 1,
		kFollowSoundId		= 2,
		kChainCount			= 3,
		kRepeatCount		= 4,
		kDescriptorLow		= 5,
		kDescriptorHigh		= 6,
		kSequenceLow		= 7,
		kSequenceHigh		= 8,
		kTicks				= 9
	};

						SoundPlayer(State& state, PsgSink* sink = NULL);

	// PLAY_SOUND (0xB4E8): the code in SOUND_NUMBER, into one or two
	// streams.
	void				Play();

	// The part of SOUND_ISR_UPDATE (0xB594) that puts the register image on
	// the chip, up to and including the delayed repeat effects.
	void				Tick();

	// The write of the thirteen registers and the mixer, on its own.
	void				WriteRegisters();

	// DISPATCH_PRIMARY_SOUND_COMMAND (0xB63E) / _SECONDARY (0xB649): run
	// commands until one asks to wait. Returns the new stream pointer.
	uint16_t			Dispatch(uint16_t stream, uint16_t pointer, bool primary);

	// ADVANCE_SOUND_STREAM_IF_READY (0xB4B5): one tick of the stream's own
	// state machine, with 'pointer' as SOUND_PTR_1 or 2. Returns the carry:
	// set means the stream has a command waiting to be dispatched.
	bool				Advance(uint16_t stream, uint16_t pointer);

	// QUEUE_SOUND_DESCRIPTOR (0xB51D): descriptor 'index' into 'stream'.
	void				Queue(uint16_t stream, uint8_t index);

	// The codes from 240 up do not queue anything: they poke the chip and the
	// voice control mask on the spot.
	void				Instant(uint8_t value);

	// The registers as they stand, for the platform and for the tests: the
	// player reads reg 7 back when it takes it over.
	void				SetPsg(uint8_t reg, uint8_t value);

			uint8_t		ReadPsg(uint8_t reg);
			void		WritePsg(uint8_t reg, uint8_t value);

private:
			void		_Reload(uint16_t stream, uint16_t descriptor);
			uint16_t	_Command(uint16_t stream, uint16_t bc,
							uint8_t command, bool primary);
			uint16_t	_Note(uint16_t stream, uint16_t bc, uint16_t hl,
							uint8_t d, uint8_t e);
			uint16_t	_Effect(uint16_t stream, uint16_t bc, uint16_t hl,
							uint8_t a, uint8_t e);
			uint8_t		_InitEffect(uint16_t state);
			void		_Mask(uint8_t d);
			void		_MixerMask(uint16_t stream, uint8_t d, uint8_t e,
							uint16_t bc);
			void		_Volume(uint8_t d, uint8_t value);

			State&		fState;
			PsgSink*	fSink;
			uint8_t		fRegs[16];
};


#endif	// ARKANOID_SOUNDPLAYER_H
