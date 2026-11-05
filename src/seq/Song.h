#pragma once
// The song: the one data model every view edits (Step screen, live recording,
// and the tracker in phase 3). Nothing gets "converted" between views.
//
// Timing: a step is a 16th note, divided into TICKS_PER_STEP ticks.
// Notes are stored sparsely per pattern, sorted by step.
#include <Arduino.h>
#include <FS.h>
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

// Tracker effects: one per step, applied to every note on it.
enum FxCmd : uint8_t {
  FX_NONE = 0,
  FX_CHANCE,    // P xx: plays with xx% probability (00-64 hex = 0-100%)
  FX_RATCHET,   // R 0n: n quick repeats inside the step (2-6)
  FX_COND,      // T xy: plays on the x-th of every y loops of the pattern
  FX_HUMANIZE,  // H xx: velocity varies randomly by up to +/- xx
  FX_MOD,       // M xx: sends mod wheel (CC1) = xx at the step
  FX_BEND,      // B xx: sends pitch bend, 40 = centre
  FX_PROGRAM,   // G xx: sends program change xx
  FX_COUNT
};

struct StepFx {
  uint8_t step;
  uint8_t cmd;
  uint8_t val;
};

struct Pattern {
  uint8_t length = 16;  // steps
  std::vector<NoteEvent> notes;  // sorted by step, then pitch
  std::vector<StepFx> fx;        // unordered, at most one per step
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

// Scenes say which pattern each track plays (or that it's silent); the
// arrangement is the order scenes play in when song mode is on.
static constexpr uint8_t SCENES = 99;
static constexpr uint8_t ARRANGE_MAX = 128;
static constexpr uint8_t SCENE_SILENT = 0xFF;

struct Scene {
  uint8_t pattern[TRACKS];  // 0-7, or SCENE_SILENT
};

struct Song {
  uint16_t bpm = 120;
  Track tracks[TRACKS];
  Scene scenes[SCENES];
  std::vector<uint8_t> arrange;  // scene indices 0-98, played in order
  bool songMode = false;         // play the arrangement instead of looping patterns
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
void setStepVelocity(Pattern& p, uint8_t step, uint8_t vel);
void setStepOffset(Pattern& p, uint8_t step, int8_t offset);
bool removeNote(Pattern& p, uint8_t step, uint8_t pitch);
// Change one note's pitch, keeping its velocity / length / offset.
bool repitchNote(Pattern& p, uint8_t step, uint8_t oldPitch, uint8_t newPitch);
void setPatternLength(Pattern& p, uint8_t length);  // drops notes past the end
// Double the pattern: length x2 with the notes copied into the new half.
// Returns false if it would exceed MAX_STEPS.
bool doublePattern(Pattern& p);
// Put a note-off on a step (replacing its notes).
void setStepOff(Pattern& p, uint8_t step);
bool isOffStep(const Pattern& p, uint8_t step);

// Length in steps of a scene: its longest non-silent pattern (16 if all silent).
uint16_t sceneSteps(uint8_t scene);

bool fxAt(const Pattern& p, uint8_t step, uint8_t* cmd, uint8_t* val);
void setFx(Pattern& p, uint8_t step, uint8_t cmd, uint8_t val);  // cmd FX_NONE removes

// ── Persistence (flash) ──────────────────────────────────────────────────────
static constexpr uint8_t SONG_NAME_MAX = 16;
enum class Storage : uint8_t { Flash, Sd };

// The mounted SD card, or nullptr if there's no card (Storage.cpp).
fs::FS* sdFs();

bool storageAvailable(Storage st);     // mounts on first use
bool validSongName(const char* name);  // letters, digits, - _ and inner spaces
const char* currentSongName();         // "" for a new, unsaved song
Storage currentSongStorage();
bool songSave();                       // to the current name + storage (false if none)
bool songSaveAs(const char* name, Storage st = Storage::Flash);
bool songLoadNamed(const char* name, Storage st = Storage::Flash);
bool songDelete(const char* name, Storage st = Storage::Flash);
bool songCopy(const char* name, Storage from, Storage to);
void songNew();
std::vector<String> songList(Storage st = Storage::Flash);  // sorted names

}  // namespace seq
