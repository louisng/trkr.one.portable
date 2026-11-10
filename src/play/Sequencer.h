#pragma once
// Plays a song (song::Song) over MIDI and the speaker preview: walks the
// arrangement step by step, applies micro-timing, swing and note lengths.
// Call update() often from loop().
#include <stdint.h>

#include "../song/Song.h"

namespace seq {

void begin();
song::Song& current();           // the loaded song (edit it, then call changed())
bool loaded();
void setLoaded(bool on);
void changed();                  // tempo / mutes edited while playing

// Move a freshly built song in (stops playback). Used by the generator.
void adopt(song::Song&& s);
// false: play the song once and stop (finished() turns true); true: loop it.
void setLoop(bool loop);
bool finished();                 // a non-looping song reached its end
// Stop a track's held notes right now (when it is muted mid-song).
void silenceTrack(uint8_t track);

// A source of patterns for endless playback (Generated mode): the sequencer
// asks for the next 4-bar pattern when the current one ends, so only a few
// patterns ever exist at once. current() then holds just the tempo and tracks
// of the song now playing, which the source updates when a new song begins.
class PatternSource {
 public:
  virtual ~PatternSource() {}
  virtual bool ready() = 0;                                   // a pattern is waiting
  virtual void take(song::Pattern& out, bool& newSong) = 0;   // newSong: it starts a new song (header already applied)
  virtual bool finished() = 0;                                // nothing more will come
};
void playStream(PatternSource* source);
uint32_t songPosition();         // steps since the current stream song began

void play(uint32_t fromStep = 0);
void stop();
bool playing();
void update();

uint32_t position();             // absolute step being played
uint16_t orderIndex();           // which arrangement slot
uint16_t bar();                  // 1-based

}  // namespace seq
