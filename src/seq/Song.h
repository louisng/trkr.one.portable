#pragma once
// The song: the one data model every view edits (Step screen, live recording,
// and the tracker in phase 3). Nothing gets "converted" between views.
//
// Timing: a step is a 16th note, divided into TICKS_PER_STEP ticks.
// Notes are stored sparsely per pattern, sorted by step.
#include <stdint.h>

#include <vector>

namespace seq {

static constexpr uint8_t TRACKS = 8;
static constexpr uint8_t PATTERNS = 8;          // per track
static constexpr uint8_t MAX_STEPS = 128;        // 8 bars
static constexpr uint8_t MAX_NOTES_PER_STEP = 4;
static constexpr uint8_t TICKS_PER_STEP = 12;   // 48 PPQN
static constexpr uint16_t BPM_MIN = 40;
static constexpr uint16_t BPM_MAX = 240;

// A step can hold an explicit note-off instead of notes: it stops whatever
// the track is still sounding (needed for tracks in hold mode).
static constexpr uint8_t PITCH_OFF = 0xFF;

enum NoteFlags : uint8_t {
  NF_SKIP_ONCE = 1,  // just recorded live: don't re-trigger it on this pass
};

struct NoteEvent {
  uint8_t step;     // 0..length-1
  int8_t offset;    // ticks relative to the step, -TPS/2 .. TPS/2-1
  uint8_t pitch;
  uint8_t vel;      // 1-127
  uint16_t len;     // ticks
  uint8_t flags;
};

struct Pattern {
  uint8_t length = 16;  // steps
  std::vector<NoteEvent> notes;  // sorted by step, then pitch
};

enum class TrackKind : uint8_t { Synth, Drum };

struct Track {
  char name[6];
  TrackKind kind;
  uint8_t channel;  // 1-16
  bool mute;
  bool hold;        // notes ring until the track's next note / OFF, ignoring length
  uint8_t pattern;  // pattern currently playing / being edited
  Pattern patterns[PATTERNS];

  Pattern& cur() { return patterns[pattern]; }
};

struct Song {
  uint16_t bpm = 120;
  Track tracks[TRACKS];
};

extern Song song;

void songInit();  // default tracks, empty patterns

// ── Editing helpers (call with the engine lock held) ─────────────────────────
// Count / collect the notes starting on a step.
uint8_t notesAt(const Pattern& p, uint8_t step, NoteEvent* out = nullptr);
void clearStep(Pattern& p, uint8_t step);
// Replace a step's notes with these pitches (max MAX_NOTES_PER_STEP).
void setStep(Pattern& p, uint8_t step, const uint8_t* pitches, uint8_t count, uint8_t vel,
             uint16_t len);
// Add one note to a step (ignored if the pitch is already there or the step is full).
bool addNote(Pattern& p, const NoteEvent& e);
// Toggle a single pitch on a step (drum hits). Returns true if it is now on.
bool toggleNote(Pattern& p, uint8_t step, uint8_t pitch, uint8_t vel, uint16_t len);
bool hasNote(const Pattern& p, uint8_t step, uint8_t pitch);
// Change the length of every note on a step by `deltaTicks` (min 1 tick).
void adjustStepLength(Pattern& p, uint8_t step, int deltaTicks);
void setStepLength(Pattern& p, uint8_t step, uint16_t lenTicks);
void setPatternLength(Pattern& p, uint8_t length);  // drops notes past the end
// Double the pattern: length x2 with the notes copied into the new half.
// Returns false if it would exceed MAX_STEPS.
bool doublePattern(Pattern& p);
// Put a note-off on a step (replacing its notes).
void setStepOff(Pattern& p, uint8_t step);
bool isOffStep(const Pattern& p, uint8_t step);

// ── Persistence (flash) ──────────────────────────────────────────────────────
bool songSave();
bool songLoad();

}  // namespace seq
