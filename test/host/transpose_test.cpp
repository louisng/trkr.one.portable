// Transpose on the device: one pattern, one step, the whole song (drums stay), out of range refused.
//   transpose_test
#include <Arduino.h>
#include <LittleFS.h>

#include "../../src/seq/Song.h"

LittleFSClass LittleFS;
namespace seq {
fs::FS* sdFs() { return nullptr; }
}  // namespace seq
using namespace seq;

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL %s\n", msg); failures++; } } while (0)

static int pitchAt(const Pattern& p, uint8_t step, int k = 0) {
  NoteEvent ev[MAX_NOTES_PER_STEP];
  return notesAt(p, step, ev) > (uint8_t)k ? ev[k].pitch : -1;
}

int main() {
  Song s;
  songInitInto(s);
  // DRUM 0, BASS 1, CHRD 2
  const uint8_t kick[] = {36}, bass[] = {36}, chord[] = {60, 64, 67};
  setStep(s.tracks[0].patterns[0], 0, kick, 1, 100, 6);
  setStep(s.tracks[1].patterns[0], 0, bass, 1, 100, 6);
  setStep(s.tracks[1].patterns[3], 4, bass, 1, 100, 6);
  setStep(s.tracks[2].patterns[0], 0, chord, 3, 100, 24);
  setStep(s.tracks[2].patterns[0], 8, chord, 3, 100, 24);
  setStepOff(s.tracks[1].patterns[0], 6);

  CHECK(transposePattern(s.tracks[2].patterns[0], 2, 8), "one step moves");
  CHECK(pitchAt(s.tracks[2].patterns[0], 8) == 62 && pitchAt(s.tracks[2].patterns[0], 8, 2) == 69, "step 9: D F# A");
  CHECK(pitchAt(s.tracks[2].patterns[0], 0) == 60, "step 1 untouched");

  CHECK(transposePattern(s.tracks[2].patterns[0], -12), "a whole pattern moves");
  CHECK(pitchAt(s.tracks[2].patterns[0], 0) == 48 && pitchAt(s.tracks[2].patterns[0], 8) == 50, "an octave down");

  CHECK(!transposePattern(s.tracks[2].patterns[0], 100), "off the top is refused");
  CHECK(pitchAt(s.tracks[2].patterns[0], 0) == 48, "and nothing changed");
  CHECK(!transposePattern(s.tracks[4].patterns[0], 1), "an empty pattern: nothing to move");

  const int n = transposeSong(s, 5);
  CHECK(n == 8, "the song: 2 bass notes + 6 chord notes");
  CHECK(pitchAt(s.tracks[0].patterns[0], 0) == 36, "the drums stay");
  CHECK(pitchAt(s.tracks[1].patterns[3], 4) == 41, "every pattern of a track, not only the one playing");
  NoteEvent ev[MAX_NOTES_PER_STEP];
  bool sawOff = false;
  for (const auto& e : s.tracks[1].patterns[0].notes) if (e.step == 6 && e.pitch == PITCH_OFF) sawOff = true;
  CHECK(sawOff, "a note-off stays a note-off");
  (void)ev;
  CHECK(transposeSong(s, -100) == -1, "off the bottom is refused");
  CHECK(pitchAt(s.tracks[1].patterns[0], 0) == 41, "and nothing changed");
  CHECK(transposeSong(s, 0) == 0, "zero does nothing");

  printf("%s\n", failures ? "FAILED" : "transpose: all ok");
  return failures != 0;
}
