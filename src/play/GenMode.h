#pragma once
// Generated mode: endless songs from the genre engine (src/gen), played over
// MIDI and the speaker. While it runs, the keyboard can play along with a
// filter that keeps you on the chord's notes (or its scale).
//
// Songs are streamed: a background task builds them one 4-bar pattern at a
// time and keeps a few patterns ready, so the device only ever holds the
// pattern that is playing plus a few waiting, never a whole song. The next
// song starts building before the current one ends, so there is no gap, and
// skipping or changing genre responds almost at once.
#include <stdint.h>

#include "../gen/Generator.h"

namespace genmode {

enum class AlongMode : uint8_t { Free, Chord, Snap, Scale };
static constexpr uint8_t TRACKS = 8;  // kick snare hats perc bass keys lead pad/arp

enum class State : uint8_t {
  Idle,        // stopped
  Generating,  // waiting for a song to be built
  Playing,
};

void begin();
void tick();  // call every loop

// Transport
void start();
void stop();
void toggle();
void next();                  // skip to a new song now
bool lowMemory();             // couldn't start: not enough free memory
int freeHeapKb();
State state();
bool running();               // Generating or Playing

// Genre: applies from the next song that is built (next() makes that now)
int genre();                  // selected genre index
int playingGenre();           // genre of the song that is playing
void setGenre(int idx);
const char* genreName(int idx);
int genreCount();

// Keep repeating the most recently built song instead of making new ones
bool locked();
void setLocked(bool on);

// Tempo of the playing song
void nudgeTempo(int delta);

// Mutes ("channels"): kept across songs
bool trackMuted(uint8_t track);
void toggleMute(uint8_t track);
void unmuteAll();
const char* trackName(uint8_t track);     // "Kick", "Bass" ... of the playing song
uint8_t trackCount();
uint8_t trackChannel(uint8_t track);

// Play-along keyboard
AlongMode along();
void setAlong(AlongMode m);
void cycleAlong();
const char* alongName(AlongMode m);
// The filter the piano keys use while Generated mode is on screen.
int filterNote(int note);
// For drawing the key map: is this pitch silent right now?
bool noteBlocked(int note);

// What is playing
const gen::Meta& meta();
const char* chordName();      // chord at the playing position
const char* sectionName();    // intro / verse / chorus ...
uint16_t currentBpm();
uint32_t currentSeed();

}  // namespace genmode
