# Arkanoid for RenkuOS
#   make          builds the tools (any POSIX system)
#   make rom      builds build/arkanoid.rom from the disassembly
#   make test     the checks that need no Haiku
#
# The game and its Haiku target come in the next phases.

CXX ?= g++
CXXFLAGS ?= -O2 -g -Wall -Wextra -std=c++17

REF = src/ref/z80/Z80.cpp src/ref/video/Tms9918.cpp \
	src/ref/msx1/Psg.cpp src/ref/msx1/Machine1.cpp
GAME = src/game/Screen.cpp src/game/Renderer.cpp

TOOLS = shot embed_rom mixer_trace effect_trace table_trace write_trace \
	render_sounds
TESTS = psg_test rom_test render_test routine_diff input_test brick_test \
	hitpoint_test hud_test playfield_test brickeffect_test sound_test \
	capsule_test brickhit_test brickmain_test motion_test vaus_test \
	levels_test lives_test soundplayer_test sound_codes_test isr_test \
	isr_trace isr_multi advance_test vm_test game_session_test frame_test \
	ball_out_test

# Every binary depends on every game header: a changed constant in State.h has
# to rebuild the tests, or a stale binary silently passes.
HEADERS = $(wildcard src/game/*.h) $(wildcard src/ref/*/*.h)
$(TOOLS) $(TESTS): $(HEADERS)

all: $(TOOLS)

# The game's own sounds, rendered to build/sounds as WAV, with the numbers
# beside them. Not part of "make test": it takes minutes and writes files.
sounds: render_sounds build/arkanoid.rom
	./render_sounds build/arkanoid.rom

shot: tools/shot.cpp $(REF) src/ref/msx1/Machine1.h src/ref/video/Tms9918.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tools/shot.cpp $(REF)

write_trace: tools/write_trace.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tools/write_trace.cpp $(REF) \
		src/game/SoundPlayer.cpp src/game/State.cpp src/game/RomImage.cpp

render_sounds: tools/render_sounds.cpp $(REF) src/game/PsgSynth.cpp \
		src/game/PsgSynth.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tools/render_sounds.cpp $(REF) \
		src/game/PsgSynth.cpp

table_trace: tools/table_trace.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tools/table_trace.cpp $(REF)

effect_trace: tools/effect_trace.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tools/effect_trace.cpp $(REF)

mixer_trace: tools/mixer_trace.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tools/mixer_trace.cpp $(REF)

embed_rom: tools/embed_rom.cpp tools/png.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tools/embed_rom.cpp

# The cartridge as data. The ROM is verified first, so the built-in data
# cannot come from anything but the original cartridge.
src/game/RomImage.cpp: build/arkanoid.rom embed_rom
	./embed_rom build/arkanoid.rom src/game/RomImage.cpp

rom_test: tests/rom_test.cpp src/game/RomImage.cpp src/game/RomImage.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/rom_test.cpp src/game/RomImage.cpp

render_test: tests/render_test.cpp $(GAME) $(REF) src/game/Screen.h \
		src/game/Renderer.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/render_test.cpp $(GAME) $(REF)

routine_diff: tests/routine_diff.cpp src/game/Ball.cpp src/game/State.cpp \
		src/game/RomImage.cpp src/game/Ball.h src/game/State.h $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/routine_diff.cpp src/game/Ball.cpp \
		src/game/State.cpp src/game/RomImage.cpp $(REF)

input_test: tests/input_test.cpp src/game/Input.cpp src/game/State.cpp \
		src/game/RomImage.cpp src/game/Input.h $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/input_test.cpp src/game/Input.cpp \
		src/game/State.cpp src/game/RomImage.cpp $(REF)

brick_test: tests/brick_test.cpp src/game/Brick.cpp src/game/State.cpp \
		src/game/RomImage.cpp src/game/Brick.h $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/brick_test.cpp src/game/Brick.cpp \
		src/game/State.cpp src/game/RomImage.cpp $(REF)

hitpoint_test: tests/hitpoint_test.cpp src/game/Collision.cpp \
		src/game/State.cpp src/game/RomImage.cpp src/game/Collision.h $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/hitpoint_test.cpp \
		src/game/Collision.cpp src/game/State.cpp src/game/RomImage.cpp $(REF)

hud_test: tests/hud_test.cpp src/game/Hud.cpp src/game/Screen.cpp \
		src/game/State.cpp src/game/RomImage.cpp src/game/Hud.h $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/hud_test.cpp src/game/Hud.cpp \
		src/game/Screen.cpp src/game/State.cpp src/game/RomImage.cpp $(REF)

playfield_test: tests/playfield_test.cpp src/game/Playfield.cpp \
		src/game/Brick.cpp src/game/Screen.cpp src/game/State.cpp \
		src/game/RomImage.cpp src/game/Playfield.h $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/playfield_test.cpp \
		src/game/Playfield.cpp src/game/Brick.cpp src/game/Screen.cpp \
		src/game/State.cpp src/game/RomImage.cpp $(REF)

brickeffect_test: tests/brickeffect_test.cpp src/game/BrickEffects.cpp \
		src/game/Ball.cpp src/game/Brick.cpp src/game/Playfield.cpp \
		src/game/Sound.cpp src/game/Capsules.cpp src/game/Random.cpp \
		src/game/Screen.cpp src/game/State.cpp \
		src/game/RomImage.cpp src/game/BrickEffects.h $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/brickeffect_test.cpp \
		src/game/BrickEffects.cpp src/game/Ball.cpp src/game/Brick.cpp \
		src/game/Playfield.cpp src/game/Sound.cpp src/game/Capsules.cpp \
		src/game/Random.cpp src/game/Screen.cpp \
		src/game/State.cpp src/game/RomImage.cpp $(REF)

psg_test: tests/psg_test.cpp src/game/PsgSynth.cpp src/game/PsgSynth.h
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/psg_test.cpp src/game/PsgSynth.cpp

advance_test: tests/advance_test.cpp src/game/SoundPlayer.cpp src/game/State.cpp \
		src/game/RomImage.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/advance_test.cpp \
		src/game/SoundPlayer.cpp src/game/State.cpp src/game/RomImage.cpp \
		$(REF)

vm_test: tests/vm_test.cpp src/game/SoundPlayer.cpp src/game/State.cpp \
		src/game/RomImage.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/vm_test.cpp \
		src/game/SoundPlayer.cpp src/game/State.cpp src/game/RomImage.cpp \
		$(REF)

isr_test: tests/isr_test.cpp src/game/SoundPlayer.cpp src/game/State.cpp \
		src/game/RomImage.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/isr_test.cpp \
		src/game/SoundPlayer.cpp src/game/State.cpp src/game/RomImage.cpp \
		$(REF)

isr_multi: tools/isr_multi.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tools/isr_multi.cpp $(REF)

isr_trace: tools/isr_trace.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tools/isr_trace.cpp $(REF)

soundplayer_test: tests/soundplayer_test.cpp src/game/SoundPlayer.cpp \
		src/game/State.cpp src/game/RomImage.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/soundplayer_test.cpp \
		src/game/SoundPlayer.cpp src/game/State.cpp src/game/RomImage.cpp \
		$(REF)

lives_test: tests/lives_test.cpp src/game/BrickEffects.cpp src/game/Ball.cpp \
		src/game/Brick.cpp src/game/Playfield.cpp src/game/Sound.cpp \
		src/game/Capsules.cpp src/game/Random.cpp src/game/Screen.cpp \
		src/game/State.cpp src/game/RomImage.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/lives_test.cpp \
		src/game/BrickEffects.cpp src/game/Ball.cpp src/game/Brick.cpp \
		src/game/Playfield.cpp src/game/Sound.cpp src/game/Capsules.cpp \
		src/game/Random.cpp src/game/Screen.cpp src/game/State.cpp \
		src/game/RomImage.cpp $(REF)

levels_test: tests/levels_test.cpp src/game/Levels.cpp src/game/Screen.cpp \
		src/game/State.cpp src/game/RomImage.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/levels_test.cpp src/game/Levels.cpp \
		src/game/Screen.cpp src/game/State.cpp src/game/RomImage.cpp $(REF)

vaus_test: tests/vaus_test.cpp src/game/Vaus.cpp src/game/BrickEffects.cpp \
		src/game/Ball.cpp src/game/Brick.cpp src/game/Playfield.cpp \
		src/game/Sound.cpp src/game/Capsules.cpp src/game/Random.cpp \
		src/game/Screen.cpp src/game/State.cpp src/game/RomImage.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/vaus_test.cpp \
		src/game/Vaus.cpp src/game/BrickEffects.cpp src/game/Ball.cpp \
		src/game/Brick.cpp src/game/Playfield.cpp src/game/Sound.cpp \
		src/game/Capsules.cpp src/game/Random.cpp src/game/Screen.cpp \
		src/game/State.cpp src/game/RomImage.cpp $(REF)

motion_test: tests/motion_test.cpp src/game/BallMotion.cpp src/game/Aliens.cpp \
		src/game/BrickCollision.cpp src/game/Collision.cpp \
		src/game/BrickEffects.cpp src/game/Ball.cpp src/game/Brick.cpp \
		src/game/Playfield.cpp src/game/Sound.cpp src/game/Capsules.cpp \
		src/game/Random.cpp src/game/Screen.cpp src/game/State.cpp \
		src/game/RomImage.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/motion_test.cpp \
		src/game/BallMotion.cpp src/game/Aliens.cpp \
		src/game/BrickCollision.cpp src/game/Collision.cpp \
		src/game/BrickEffects.cpp src/game/Ball.cpp src/game/Brick.cpp \
		src/game/Playfield.cpp src/game/Sound.cpp src/game/Capsules.cpp \
		src/game/Random.cpp src/game/Screen.cpp src/game/State.cpp \
		src/game/RomImage.cpp $(REF)

brickmain_test: tests/brickmain_test.cpp src/game/BrickCollision.cpp \
		src/game/Collision.cpp src/game/BrickEffects.cpp src/game/Ball.cpp \
		src/game/Brick.cpp src/game/Playfield.cpp src/game/Sound.cpp \
		src/game/Capsules.cpp src/game/Random.cpp src/game/Screen.cpp \
		src/game/State.cpp src/game/RomImage.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/brickmain_test.cpp \
		src/game/BrickCollision.cpp src/game/Collision.cpp \
		src/game/BrickEffects.cpp src/game/Ball.cpp src/game/Brick.cpp \
		src/game/Playfield.cpp src/game/Sound.cpp src/game/Capsules.cpp \
		src/game/Random.cpp src/game/Screen.cpp src/game/State.cpp \
		src/game/RomImage.cpp $(REF)

brickhit_test: tests/brickhit_test.cpp src/game/BrickCollision.cpp \
		src/game/Collision.cpp src/game/BrickEffects.cpp src/game/Ball.cpp \
		src/game/Brick.cpp src/game/Playfield.cpp src/game/Sound.cpp \
		src/game/Capsules.cpp src/game/Random.cpp src/game/Screen.cpp \
		src/game/State.cpp src/game/RomImage.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/brickhit_test.cpp \
		src/game/BrickCollision.cpp src/game/Collision.cpp \
		src/game/BrickEffects.cpp src/game/Ball.cpp src/game/Brick.cpp \
		src/game/Playfield.cpp src/game/Sound.cpp src/game/Capsules.cpp \
		src/game/Random.cpp src/game/Screen.cpp src/game/State.cpp \
		src/game/RomImage.cpp $(REF)

capsule_test: tests/capsule_test.cpp src/game/Capsules.cpp \
		src/game/Random.cpp src/game/Screen.cpp src/game/State.cpp \
		src/game/RomImage.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/capsule_test.cpp \
		src/game/Capsules.cpp src/game/Random.cpp src/game/Screen.cpp \
		src/game/State.cpp src/game/RomImage.cpp $(REF)

game_session_test: tests/game_session_test.cpp src/game/Aliens.cpp src/game/Ball.cpp src/game/BallMotion.cpp src/game/Brick.cpp src/game/BrickCollision.cpp src/game/BrickEffects.cpp src/game/Capsules.cpp src/game/Collision.cpp src/game/Game.cpp src/game/Hud.cpp src/game/Input.cpp src/game/Levels.cpp src/game/Playfield.cpp src/game/Random.cpp src/game/Renderer.cpp src/game/RomImage.cpp src/game/Screen.cpp src/game/Sound.cpp src/game/SoundPlayer.cpp src/game/State.cpp src/game/Vaus.cpp \
		$(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/game_session_test.cpp src/game/Aliens.cpp src/game/Ball.cpp src/game/BallMotion.cpp src/game/Brick.cpp src/game/BrickCollision.cpp src/game/BrickEffects.cpp src/game/Capsules.cpp src/game/Collision.cpp src/game/Game.cpp src/game/Hud.cpp src/game/Input.cpp src/game/Levels.cpp src/game/Playfield.cpp src/game/Random.cpp src/game/Renderer.cpp src/game/RomImage.cpp src/game/Screen.cpp src/game/Sound.cpp src/game/SoundPlayer.cpp src/game/State.cpp src/game/Vaus.cpp \
		$(REF)

frame_test: tests/frame_test.cpp src/game/Aliens.cpp src/game/Ball.cpp src/game/BallMotion.cpp src/game/Brick.cpp src/game/BrickCollision.cpp src/game/BrickEffects.cpp src/game/Capsules.cpp src/game/Collision.cpp src/game/Game.cpp src/game/Hud.cpp src/game/Input.cpp src/game/Levels.cpp src/game/Playfield.cpp src/game/Random.cpp src/game/Renderer.cpp src/game/RomImage.cpp src/game/Screen.cpp src/game/Sound.cpp src/game/SoundPlayer.cpp src/game/State.cpp src/game/Vaus.cpp \
		$(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/frame_test.cpp src/game/Aliens.cpp src/game/Ball.cpp src/game/BallMotion.cpp src/game/Brick.cpp src/game/BrickCollision.cpp src/game/BrickEffects.cpp src/game/Capsules.cpp src/game/Collision.cpp src/game/Game.cpp src/game/Hud.cpp src/game/Input.cpp src/game/Levels.cpp src/game/Playfield.cpp src/game/Random.cpp src/game/Renderer.cpp src/game/RomImage.cpp src/game/Screen.cpp src/game/Sound.cpp src/game/SoundPlayer.cpp src/game/State.cpp src/game/Vaus.cpp \
		$(REF)

ball_out_test: tests/ball_out_test.cpp src/game/Aliens.cpp src/game/Ball.cpp src/game/BallMotion.cpp src/game/Brick.cpp src/game/BrickCollision.cpp src/game/BrickEffects.cpp src/game/Capsules.cpp src/game/Collision.cpp src/game/Game.cpp src/game/Hud.cpp src/game/Input.cpp src/game/Levels.cpp src/game/Playfield.cpp src/game/Random.cpp src/game/Renderer.cpp src/game/RomImage.cpp src/game/Screen.cpp src/game/Sound.cpp src/game/SoundPlayer.cpp src/game/State.cpp src/game/Vaus.cpp \
		$(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/ball_out_test.cpp src/game/Aliens.cpp src/game/Ball.cpp src/game/BallMotion.cpp src/game/Brick.cpp src/game/BrickCollision.cpp src/game/BrickEffects.cpp src/game/Capsules.cpp src/game/Collision.cpp src/game/Game.cpp src/game/Hud.cpp src/game/Input.cpp src/game/Levels.cpp src/game/Playfield.cpp src/game/Random.cpp src/game/Renderer.cpp src/game/RomImage.cpp src/game/Screen.cpp src/game/Sound.cpp src/game/SoundPlayer.cpp src/game/State.cpp src/game/Vaus.cpp \
		$(REF)

sound_codes_test: tests/sound_codes_test.cpp src/game/Sound.cpp \
		src/game/SoundPlayer.cpp src/game/State.cpp src/game/RomImage.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/sound_codes_test.cpp \
		src/game/Sound.cpp src/game/SoundPlayer.cpp src/game/State.cpp \
		src/game/RomImage.cpp $(REF)

sound_test: tests/sound_test.cpp src/game/Sound.cpp \
		src/game/BrickEffects.cpp src/game/Ball.cpp src/game/Brick.cpp \
		src/game/Playfield.cpp src/game/Capsules.cpp src/game/Random.cpp \
		src/game/Screen.cpp src/game/State.cpp \
		src/game/RomImage.cpp $(REF)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/sound_test.cpp \
		src/game/Sound.cpp src/game/BrickEffects.cpp src/game/Ball.cpp \
		src/game/Brick.cpp src/game/Playfield.cpp src/game/Capsules.cpp \
		src/game/Random.cpp src/game/Screen.cpp \
		src/game/State.cpp src/game/RomImage.cpp $(REF)

# The ROM needs the disassembly, which is not part of this repository.
build/arkanoid.rom:
	tools/build_rom.sh

rom: build/arkanoid.rom

test: $(TESTS) $(TOOLS) build/arkanoid.rom
# Game.cpp ties the core together and is only compiled for real on the
# target, where the platform layer lives. Syntax-checking it here means a
# change to its interface cannot rot unnoticed.
	$(CXX) $(CXXFLAGS) -Isrc -fsyntax-only src/game/Game.cpp
	./psg_test
	./rom_test build/arkanoid.rom
	./render_test build/arkanoid.rom 300
	./routine_diff build/arkanoid.rom 1000
	./input_test build/arkanoid.rom 400
	./brick_test build/arkanoid.rom
	./hitpoint_test build/arkanoid.rom 300
	./hud_test build/arkanoid.rom 200
	./playfield_test build/arkanoid.rom 400
	./brickeffect_test build/arkanoid.rom 300
	./sound_test build/arkanoid.rom 400
	./sound_codes_test build/arkanoid.rom
	./game_session_test build/arkanoid.rom
	./capsule_test build/arkanoid.rom 200
	./brickhit_test build/arkanoid.rom 400
	./brickmain_test build/arkanoid.rom 300
	./motion_test build/arkanoid.rom 200
	./vaus_test build/arkanoid.rom 400
	./levels_test build/arkanoid.rom 300
	./lives_test build/arkanoid.rom 200
	./soundplayer_test build/arkanoid.rom 300
	./isr_test build/arkanoid.rom 400
	./ball_out_test build/arkanoid.rom
	./advance_test build/arkanoid.rom 400
# The same sweep again, one write journal at a time: every tick's writes to the
# sound window compared in order, which names the instruction that differs.
# Two ticks still differ, on two codes; see docs/01-progress.md.
# Every sound code, forty ticks each: the whole tick, commands included. A code
	# whose stream pointer leaves the cartridge is skipped and counted, because
	# there the Z80 reads the BIOS and the port has none; every code that stays
	# has to agree exactly.
	./vm_test build/arkanoid.rom 0 28 0
# The same sweep with every register seeded with 0xff before each ISR call: a
# routine that reads a register it never wrote would show up here as a changed
# journal, which is how the mask's stale D was found.
	REGS=ff ./write_trace build/arkanoid.rom sweep 0 28 0
	./write_trace build/arkanoid.rom sweep 0 28 0
	./vm_test build/arkanoid.rom 0 256 0
	./write_trace build/arkanoid.rom sweep 0 256 0

check-title: rom shot
	./shot build/arkanoid.rom 300 build/title.png -vram build/title.vram

clean:
	rm -f $(TOOLS) $(TESTS)
	rm -rf build

.PHONY: all rom test check-title clean
