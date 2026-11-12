#pragma once
// Sequencer engine: clock task, transport, playback, live recording.
//
// The clock runs in its own FreeRTOS task so screen drawing never delays
// notes. Everything that touches the song, MIDI output or the speaker takes
// the engine lock (seq::Lock) first.
#include <stdint.h>

#include "Song.h"

namespace seq {

void engineBegin();

// Recursive lock shared by the clock task and the UI.
void lock();
void unlock();
struct Lock {
  Lock() { lock(); }
  ~Lock() { unlock(); }
};

// Transport
void play();
void stop();
bool playing();
void setRecordArmed(bool on);
bool recordArmed();
bool recording();  // armed and playing

// Track selection / routing of live playing
uint8_t selectedTrack();
void selectTrack(uint8_t t);
uint8_t liveSynthTrack();  // where piano/chord keys go: the selected track if it's a synth
uint8_t liveDrumTrack();   // where drum pads go: the selected track if it's drums

// Stop anything a track is still sounding (e.g. held notes when hold mode,
// mute or the channel changes).
void releaseTrack(uint8_t t);

// Tap tempo: call on each tap; sets song.bpm after two or more taps.
// Returns the number of taps in the current run.
uint8_t tapTempo();

// Song mode (song.songMode): play the arrangement. Start position for play(),
// the arrangement row playing now (-1 if not in song mode / stopped), and
// switching every track to a scene's patterns right away.
void setSongStart(uint8_t pos);
int songPosition();
void applyScene(uint8_t scene);

// Called (with the engine lock held) at the moment the arrangement runs out
// and is about to start again from its first row. Generated mode uses it to
// swap in the next song exactly on the beat. nullptr = none.
typedef void (*WrapHook)();
void setWrapHook(WrapHook hook);
// Steps (16ths) played since the arrangement started, or -1 if not in song mode / stopped.
int32_t songStep();

// Current step of a track's pattern, or -1 when stopped.
int trackStep(uint8_t t);

// Live recording (called by the UI for every musical key press/release while
// recording). `track` is where the notes go.
void recordPress(uint8_t key, uint8_t track, const uint8_t* pitches, uint8_t count, uint8_t vel);
void recordRelease(uint8_t key);

}  // namespace seq
