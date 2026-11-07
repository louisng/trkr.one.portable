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

void play(uint32_t fromStep = 0);
void stop();
bool playing();
void update();

uint32_t position();             // absolute step being played
uint16_t orderIndex();           // which arrangement slot
uint16_t bar();                  // 1-based

}  // namespace seq
