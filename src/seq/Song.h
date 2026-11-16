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
static constexpr uint8_t PATTERNS = 24;         // per track (the file format kept 8 before v7)
static constexpr uint8_t LEGACY_PATTERNS = 8;   // songs saved by older versions hold 8 per track
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
  FX_STRUM,     // S n: strum the step's notes like a guitar. n = 1-3 down, 4-6 up; fast / medium / slow
  FX_COUNT
};

// Strum (FX_STRUM): 1 2 3 = down (lowest note first), 4 5 6 = up, each fast / medium / slow.
// Notes follow each other 1, 2 or 4 ticks apart (the website's 2, 4 or 8 in 1/24ths of a step), and each
// later note is STRUM_FALL softer. trkr.one plays strums the same way.
static constexpr uint8_t STRUM_FALL = 6;
inline bool strumValid(uint8_t v) { return v >= 1 && v <= 6; }
inline bool strumUp(uint8_t v) { return v > 3; }
inline uint8_t strumGapTicks(uint8_t v) { return (uint8_t)(1u << ((v - 1) % 3)); }
// Of n notes (lowest first), the place of note i in the strum: 0 sounds first.
inline uint8_t strumIndex(uint8_t v, uint8_t i, uint8_t n) { return strumUp(v) ? (uint8_t)(n - 1 - i) : i; }

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

// The built-in synth's sound for a track. All values 0-127 unless noted.
// Drum tracks use sound, volume, cutoff (tone), decay and octave (pitch,
// in semitones) to shape the drum kit.
enum Wave : uint8_t { WAVE_SAW, WAVE_SQUARE, WAVE_PULSE, WAVE_TRIANGLE, WAVE_SINE, WAVE_NOISE, WAVE_COUNT };

struct Instrument {
  uint8_t sound;      // 1 = play on the built-in synth, 0 = MIDI only
  uint8_t volume;
  uint8_t wave;       // Wave
  int8_t octave;      // -2..+2 (drums: pitch -12..+12 semitones)
  uint8_t detune;     // second oscillator detune, 0 = single oscillator
  uint8_t attack, decay, sustain, release;
  uint8_t cutoff, resonance, envMod;  // filter, and how far the envelope opens it
};

static constexpr uint8_t PRESET_COUNT = 8;
const char* presetName(uint8_t preset);
void applyPreset(Instrument& in, uint8_t preset);
void defaultInstrument(Instrument& in, uint8_t track, bool drum);

// ── Drum MIDI routing ────────────────────────────────────────────────────────
// Inside patterns, drum hits are stored by their lane's General MIDI note
// (kick 36, snare 38, ...). What actually goes out over MIDI depends on the
// track's drum routing:
//   kit:   every lane on the track's channel, each lane on kitNote[lane]
//   lanes: every lane on its own channel laneCh[lane], playing laneNote[lane]
//          (for gear like the M8 where each instrument has its own channel)
static constexpr uint8_t DRUM_LANES = 12;
extern const uint8_t DRUM_LANE_ID[DRUM_LANES];  // GM note that identifies each lane
enum DrumMode : uint8_t { DRUM_KIT, DRUM_LANES_MODE };
int drumLaneOf(uint8_t pitch);                  // -1 if not a lane note

struct DrumRouting {
  uint8_t mode;                  // DrumMode
  uint8_t kitNote[DRUM_LANES];
  uint8_t laneCh[DRUM_LANES];    // 1-16
  uint8_t laneNote[DRUM_LANES];
};
void drumRoutingGmKit(DrumRouting& r);            // kit mode, standard GM notes
void drumRoutingPerLane(DrumRouting& r, uint8_t firstCh = 1, uint8_t note = 60);

struct Track {
  char name[6];
  TrackKind kind;
  uint8_t channel;  // 1-16
  bool mute;
  bool hold;        // notes ring until the track's next note / OFF, ignoring length
  uint8_t pattern;  // pattern currently playing / being edited
  Instrument inst;  // built-in synth sound
  DrumRouting drums;  // how drum hits go out over MIDI (drum tracks)
  Pattern patterns[PATTERNS];

  Pattern& cur() { return patterns[pattern]; }
};

// Scenes say which pattern each track plays (or that it's silent); the
// arrangement is the order scenes play in when song mode is on.
static constexpr uint8_t SCENES = 99;
static constexpr uint8_t ARRANGE_MAX = 128;
static constexpr uint8_t SCENE_SILENT = 0xFF;

// One character for a pattern number: 1-9, then A, B, C ... ('-' if none).
inline char patternChar(uint8_t pt) {
  return pt < 9 ? (char)('1' + pt) : pt < PATTERNS ? (char)('A' + (pt - 9)) : '-';
}

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
void songInitInto(Song& s);  // the same for a song held in its own object

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

// MIDI channel + note a hit on this track goes out as.
void midiOut(const Track& tr, uint8_t pitch, uint8_t* ch, uint8_t* note);

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
// For the cloud sync: read / write a song in its own object (same file format),
// leaving the open song alone. Flash or SD, by name.
bool songSaveCopy(const char* name, const Song& s, Storage st = Storage::Flash);
bool songLoadCopy(const char* name, Song& out, Storage st = Storage::Flash);
// The open song no longer belongs to a saved file (so saving asks for a name
// instead of overwriting the old one). Used when a song is generated.
void songForgetName();
void songNew();
std::vector<String> songList(Storage st = Storage::Flash);  // sorted names

}  // namespace seq
