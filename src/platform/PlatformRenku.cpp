/*
 * Copyright 2026, Akakor.
 * All rights reserved. Distributed under the terms of the MIT License.
 */


// The RenkuOS window.
//
// This is the only file that knows about the Be API. It owns the window, turns
// key events into the MSX keyboard matrix the game reads, and calls the game's
// tick from a message runner that fires sixty times a second.
//
// The picture is 256x192 in sixteen colours; it is drawn into a BBitmap and
// stretched to the window by DrawBitmap, so the window can be any size and the
// game never knows.
//
// NOTE: this file cannot be compiled in the Linux worktree the rest of the port
// is built in - there are no Be headers here. It is written against the Be API
// as RenkuOS has it and has to be built on the target.


#include <Application.h>
#include <Bitmap.h>
#include <Cursor.h>
#include <Locker.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Screen.h>
#include <View.h>
#include <Window.h>

#include <stdio.h>
#include <string.h>

#include "game/Game.h"
#include "game/Input.h"
#include "game/PsgSynth.h"

#include <SoundPlayer.h>


namespace {

const bigtime_t kTickMicroseconds = 16667;	// sixty frames a second
const int32 kScale = 3;

const uint32 kMsgTick = 'tick';


// The keys the game uses, as the MSX keyboard matrix numbers them: row, bit.
struct KeyBinding {
	uint32	code;
	int		row;
	int		bit;
};

const KeyBinding kBindings[] = {
	{ B_UP_ARROW,		8, 3 },		// up
	{ B_DOWN_ARROW,		8, 2 },		// down
	{ B_LEFT_ARROW,		8, 1 },		// left
	{ B_RIGHT_ARROW,	8, 0 },		// right
	{ B_SPACE,			7, 0 },		// space
	{ 'z',				7, 2 },		// trigger A, the paddle button
	{ 'x',				7, 1 },		// trigger B
	{ B_RETURN,			7, 6 }		// the paddle's start button
};

}


const int kSampleRate = 44100;


// What the node is asked for: 44.1 kHz, sixteen bits, two channels, in the
// order the synthesiser writes them.
const media_raw_audio_format&
AudioFormat()
{
	static const media_raw_audio_format format = [] {
		media_raw_audio_format result;
		result.frame_rate = kSampleRate;
		result.channel_count = 2;
		result.format = media_raw_audio_format::B_AUDIO_SHORT;
		result.byte_order = B_MEDIA_LITTLE_ENDIAN;
		result.buffer_size = 4096;
		return result;
	}();
	return format;
}


// Where the driver's PSG writes arrive, and where the samples come from.
//
// The driver writes registers from the game thread while the audio node renders
// from its own, so both sides take the lock. The synthesis itself is in
// PsgSynth, which knows nothing about the Be API and is tested on its own.
class PsgOutput : public SoundPlayer::PsgSink {
public:
						PsgOutput()
							:
							fSynth(kSampleRate)
						{
						}

	virtual	void		WritePsg(uint8_t reg, uint8_t value)
						{
							BAutolock lock(fLock);
							fSynth.WritePsg(reg, value);
						}

	virtual	uint8_t		ReadPsg(uint8_t reg)
						{
							BAutolock lock(fLock);
							return fSynth.ReadPsg(reg);
						}

	// Called from the sound thread, with the format the node was made with:
	// signed 16-bit stereo.
	void				Render(int16_t* samples, size_t frames)
						{
							BAutolock lock(fLock);
							fSynth.Render(samples, frames);
						}

private:
			BLocker		fLock;
			PsgSynth	fSynth;
};


// The node that asks for those samples.
class AudioNode : public BSoundPlayer {
public:
						AudioNode(PsgOutput& output);

	static	void		Fill(void* cookie, void* buffer, size_t size,
							const media_raw_audio_format& format);
};


AudioNode::AudioNode(PsgOutput& output)
	:
	BSoundPlayer(&AudioFormat(), "arkanoid sound", &AudioNode::Fill, NULL,
		&output)
{
	Start();
}


void
AudioNode::Fill(void* cookie, void* buffer, size_t size,
	const media_raw_audio_format& format)
{
	PsgOutput* output = static_cast<PsgOutput*>(cookie);
	if (output == NULL || buffer == NULL)
		return;

	// Both channels are sixteen bits, so a frame is four bytes.
	const size_t frameSize = (format.channel_count * sizeof(int16_t));
	if (frameSize == 0)
		return;

	output->Render(static_cast<int16_t*>(buffer), size / frameSize);
}


class GameView : public BView {
public:
						GameView(BRect frame);
	virtual				~GameView();

	virtual	void		AttachedToWindow();
	virtual	void		MakeFocus(bool focused = true);
	virtual	void		Draw(BRect updateRect);
	virtual	void		MessageReceived(BMessage* message);
	virtual	void		KeyDown(const char* bytes, int32 numBytes);
	virtual	void		KeyUp(const char* bytes, int32 numBytes);

private:
	void				_MakeBitmap();
	void				_SetKey(uint32 code, bool down);
	void				_Redraw();

			PsgOutput	fPsg;
			Game		fGame;
			AudioNode*	fAudio;
			BBitmap*	fBitmap;
			KeyMatrix	fKeys;
			BMessageRunner* fRunner;
};


GameView::GameView(BRect frame)
	:
	BView(frame, "game", B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS
								| B_PULSE_NEEDED),
	fGame(&fPsg),
	fAudio(NULL),
	fBitmap(NULL),
	fRunner(NULL)
{
	fKeys.Clear();
	_MakeBitmap();
}


GameView::~GameView()
{
	delete fRunner;
	delete fAudio;
	delete fBitmap;
}


void
GameView::_MakeBitmap()
{
	fBitmap = new BBitmap(BRect(0, 0, Game::kWidth - 1, Game::kHeight - 1),
		B_RGB32);
	// Bits() is NULL on a bitmap the server refused, and Draw writes straight
	// through it: a window with no picture is better than a crash.
	if (!fBitmap->IsValid()) {
		fprintf(stderr, "cannot allocate the %dx%d game bitmap\n", Game::kWidth,
			Game::kHeight);
		delete fBitmap;
		fBitmap = NULL;
	}
}


void
GameView::AttachedToWindow()
{
	// Focus belongs to the window, so it is asked for here rather than in the
	// constructor. AttachedToWindow can be called again after a detach, and the
	// old runner would go on ticking a view that is no longer on screen.
	delete fRunner;
	BMessage tick(kMsgTick);
	fRunner = new BMessageRunner(BMessenger(this), &tick,
		kTickMicroseconds);

	// The sound runs on its own clock, fed by the driver's register writes;
	// it does not wait for the game's tick to ask for samples.
	if (fAudio == NULL)
		fAudio = new AudioNode(fPsg);

	MakeFocus(true);
}


void
GameView::MakeFocus(bool focused)
{
	// Losing focus means the KeyUp for a held key never arrives, and the paddle
	// would go on moving by itself.
	if (!focused)
		fKeys.Clear();
	BView::MakeFocus(focused);
}


void
GameView::Draw(BRect updateRect)
{
	if (fBitmap == NULL)
		return;

	const uint8_t* frame = fGame.Frame(false);
	uint8* bits = static_cast<uint8*>(fBitmap->Bits());
	const int32 bytesPerRow = fBitmap->BytesPerRow();

	for (int y = 0; y < Game::kHeight; y++) {
		uint8* row = bits + y * bytesPerRow;
		for (int x = 0; x < Game::kWidth; x++) {
			const uint32 rgb = Game::Colour(frame[y * Game::kWidth + x]);
			// The palette holds 0xRRGGBB and B_RGB32 is little endian, so the
			// bytes go out as blue, green, red, unused - blue first.
			row[x * 4 + 0] = rgb & 0xff;
			row[x * 4 + 1] = (rgb >> 8) & 0xff;
			row[x * 4 + 2] = (rgb >> 16) & 0xff;
			row[x * 4 + 3] = 0xff;
		}
	}

	SetDrawingMode(B_OP_COPY);
	// Nearest, not bilinear: the picture is 256x192 stretched by a whole number,
	// and the original's pixels are meant to stay square.
	DrawBitmap(fBitmap, BRect(0, 0, Game::kWidth - 1, Game::kHeight - 1),
		Bounds(), B_FILTER_BITMAP_NONE);
}


void
GameView::MessageReceived(BMessage* message)
{
	if (message->what == kMsgTick) {
		fGame.Tick(fKeys, 0, 0);
		_Redraw();
		return;
	}
	BView::MessageReceived(message);
}


void
GameView::_Redraw()
{
	// The whole picture changes every frame, so the whole view is redrawn.
	// Invalidate is enough: the window calls Draw from its own loop.
	Invalidate();
}


void
GameView::_SetKey(uint32 code, bool down)
{
	for (size_t i = 0; i < sizeof(kBindings) / sizeof(kBindings[0]); i++) {
		if (kBindings[i].code != code)
			continue;
		fKeys.Set(kBindings[i].row, kBindings[i].bit, down);
		return;
	}
}


void
GameView::KeyDown(const char* bytes, int32 numBytes)
{
	if (numBytes > 0)
		_SetKey(static_cast<uint8>(bytes[0]), true);
}


void
GameView::KeyUp(const char* bytes, int32 numBytes)
{
	if (numBytes > 0)
		_SetKey(static_cast<uint8>(bytes[0]), false);
}


class GameWindow : public BWindow {
public:
						GameWindow()
							:
							BWindow(BRect(100, 100,
								static_cast<float>(100 + kScale * Game::kWidth) - 1,
								static_cast<float>(100 + kScale * Game::kHeight) - 1),
								"Arkanoid", B_TITLED_WINDOW, B_NOT_RESIZABLE
									| B_QUIT_ON_WINDOW_CLOSE)
						{
							fView = new GameView(Bounds());
							AddChild(fView);
						}

	virtual	bool		QuitRequested() { be_app->PostMessage(B_QUIT_REQUESTED); return true; }

private:
			GameView*	fView;
};


class GameApplication : public BApplication {
public:
						GameApplication()
							:
							BApplication("application/x-vnd.Akakor-Arkanoid")
						{
						}

	virtual	void		ReadyToRun()
						{
							fWindow = new GameWindow();
							fWindow->Show();
						}

private:
			GameWindow*	fWindow;
};


int
main(int argc, char** argv)
{
	GameApplication app;
	app.Run();
	return 0;
}
