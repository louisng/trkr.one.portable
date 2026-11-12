#pragma once
// Generated mode: endless songs made on the device by the same generator as
// trkr.one (same seed, same song), played by the normal sequencer.
//
// A song is built as an ordinary device song (scenes + arrangement), so
// everything else works on it: fn+1..8 mutes parts, per-track MIDI channels,
// the built-in synth, even saving it (fn+s) or editing it. While one song
// plays, the next is built in the background and swapped in exactly when the
// arrangement wraps around. Mutes and channel changes carry over to the next
// song.
#include "../gen/Along.h"
#include "../gen/Generator.h"

namespace genmode {

enum class State : uint8_t {
  Idle,        // off
  Generating,  // building the first song
  Playing,     // running (the sequencer may be paused with space)
};

void begin();
void tick();            // call every loop (starts the first song when it is ready)
void start();           // make a song and play, endlessly
void stop();
void next();            // skip to a new song now
bool running();
State state();
bool lowMemory();       // couldn't build: not enough free memory
bool rebuilding();      // the playing song was freed to make room: the next one is being built (a gap)
bool prefetchBlocked(); // too little RAM to build the next song while this one plays
int freeHeapKb();

int genreCount();
const char* genreName(int idx);
int genre();            // chosen for the next songs
void setGenre(int idx);
int playingGenre();

const gen::Meta& meta();        // of the song playing
const gen::ChordSpan* chordNow();  // chord at the current step, or nullptr
gen::Along along();
void setAlong(gen::Along a);
const char* alongName(gen::Along a);

}  // namespace genmode
