#include "Song.h"

#include <FS.h>
#include <LittleFS.h>
#include <string.h>

#include <algorithm>

namespace seq {

Song song;

struct TrackDefault {
  const char* name;
  TrackKind kind;
  uint8_t channel;
};

static const TrackDefault DEFAULTS[TRACKS] = {
    {"DRUM", TrackKind::Drum, 10}, {"BASS", TrackKind::Synth, 2}, {"CHRD", TrackKind::Synth, 3},
    {"LEAD", TrackKind::Synth, 4}, {"SYN5", TrackKind::Synth, 5}, {"SYN6", TrackKind::Synth, 6},
    {"SYN7", TrackKind::Synth, 7}, {"SYN8", TrackKind::Synth, 8},
};

void songInit() {
  song.bpm = 120;
  for (uint8_t t = 0; t < TRACKS; t++) {
    Track& tr = song.tracks[t];
    strncpy(tr.name, DEFAULTS[t].name, sizeof(tr.name) - 1);
    tr.name[sizeof(tr.name) - 1] = 0;
    tr.kind = DEFAULTS[t].kind;
    tr.channel = DEFAULTS[t].channel;
    tr.mute = false;
    tr.pattern = 0;
    for (auto& p : tr.patterns) {
      p.length = 16;
      p.notes.clear();
    }
  }
}

// ── Editing ──────────────────────────────────────────────────────────────────
static bool lessEv(const NoteEvent& a, const NoteEvent& b) {
  return a.step != b.step ? a.step < b.step : a.pitch < b.pitch;
}

uint8_t notesAt(const Pattern& p, uint8_t step, NoteEvent* out) {
  uint8_t n = 0;
  for (const auto& e : p.notes) {
    if (e.step == step) {
      if (out && n < MAX_NOTES_PER_STEP) out[n] = e;
      n++;
    } else if (e.step > step) {
      break;
    }
  }
  return n;
}

bool hasNote(const Pattern& p, uint8_t step, uint8_t pitch) {
  for (const auto& e : p.notes)
    if (e.step == step && e.pitch == pitch) return true;
  return false;
}

void clearStep(Pattern& p, uint8_t step) {
  p.notes.erase(std::remove_if(p.notes.begin(), p.notes.end(),
                               [step](const NoteEvent& e) { return e.step == step; }),
                p.notes.end());
}

bool addNote(Pattern& p, const NoteEvent& e) {
  if (e.step >= p.length) return false;
  if (hasNote(p, e.step, e.pitch)) return false;
  if (notesAt(p, e.step) >= MAX_NOTES_PER_STEP) return false;
  auto it = std::upper_bound(p.notes.begin(), p.notes.end(), e, lessEv);
  p.notes.insert(it, e);
  return true;
}

void setStep(Pattern& p, uint8_t step, const uint8_t* pitches, uint8_t count, uint8_t vel,
             uint16_t len) {
  clearStep(p, step);
  for (uint8_t i = 0; i < count && i < MAX_NOTES_PER_STEP; i++)
    addNote(p, NoteEvent{step, 0, pitches[i], vel, len, 0});
}

bool toggleNote(Pattern& p, uint8_t step, uint8_t pitch, uint8_t vel, uint16_t len) {
  for (auto it = p.notes.begin(); it != p.notes.end(); ++it) {
    if (it->step == step && it->pitch == pitch) {
      p.notes.erase(it);
      return false;
    }
  }
  return addNote(p, NoteEvent{step, 0, pitch, vel, len, 0});
}

void adjustStepLength(Pattern& p, uint8_t step, int deltaTicks) {
  const int maxLen = (int)p.length * TICKS_PER_STEP;
  for (auto& e : p.notes) {
    if (e.step != step) continue;
    int l = (int)e.len + deltaTicks;
    if (l < 1) l = 1;
    if (l > maxLen) l = maxLen;
    e.len = (uint16_t)l;
  }
}

void setPatternLength(Pattern& p, uint8_t length) {
  if (length < 1) length = 1;
  if (length > MAX_STEPS) length = MAX_STEPS;
  p.length = length;
  p.notes.erase(std::remove_if(p.notes.begin(), p.notes.end(),
                               [length](const NoteEvent& e) { return e.step >= length; }),
                p.notes.end());
}

// ── Persistence ──────────────────────────────────────────────────────────────
// File layout (little endian):
//   "CMSG" u8 version  u16 bpm
//   per track: name[6] kind channel mute pattern
//     per pattern: length u16 count, then count x {step offset pitch vel u16 len}
static const char* SONG_PATH = "/song.cms";
static constexpr uint8_t SONG_VERSION = 1;
static bool fsReady = false;

static bool ensureFs() {
  if (!fsReady) fsReady = LittleFS.begin(true);  // formats the partition on first use
  return fsReady;
}

bool songSave() {
  if (!ensureFs()) return false;
  File f = LittleFS.open("/song.tmp", "w");
  if (!f) return false;
  f.write((const uint8_t*)"CMSG", 4);
  f.write(SONG_VERSION);
  f.write((const uint8_t*)&song.bpm, 2);
  for (const auto& tr : song.tracks) {
    f.write((const uint8_t*)tr.name, 6);
    f.write((uint8_t)tr.kind);
    f.write(tr.channel);
    f.write((uint8_t)tr.mute);
    f.write(tr.pattern);
    for (const auto& p : tr.patterns) {
      f.write(p.length);
      uint16_t count = (uint16_t)p.notes.size();
      f.write((const uint8_t*)&count, 2);
      for (const auto& e : p.notes) {
        uint8_t rec[6] = {e.step, (uint8_t)e.offset, e.pitch, e.vel, (uint8_t)(e.len & 0xFF),
                          (uint8_t)(e.len >> 8)};
        f.write(rec, 6);
      }
    }
  }
  bool ok = f.getWriteError() == 0;
  f.close();
  if (!ok) return false;
  LittleFS.remove(SONG_PATH);
  return LittleFS.rename("/song.tmp", SONG_PATH);
}

bool songLoad() {
  if (!ensureFs() || !LittleFS.exists(SONG_PATH)) return false;
  File f = LittleFS.open(SONG_PATH, "r");
  if (!f) return false;
  char magic[4];
  uint8_t ver = 0;
  uint16_t bpm = 0;
  bool ok = f.read((uint8_t*)magic, 4) == 4 && memcmp(magic, "CMSG", 4) == 0 &&
            f.read(&ver, 1) == 1 && ver == SONG_VERSION && f.read((uint8_t*)&bpm, 2) == 2;
  if (!ok) {
    f.close();
    return false;
  }
  songInit();
  song.bpm = (bpm >= BPM_MIN && bpm <= BPM_MAX) ? bpm : 120;
  for (auto& tr : song.tracks) {
    uint8_t hdr[4];
    if (f.read((uint8_t*)tr.name, 6) != 6 || f.read(hdr, 4) != 4) break;
    tr.name[5] = 0;
    tr.kind = hdr[0] == 1 ? TrackKind::Drum : TrackKind::Synth;
    tr.channel = (hdr[1] >= 1 && hdr[1] <= 16) ? hdr[1] : 1;
    tr.mute = hdr[2] != 0;
    tr.pattern = hdr[3] < PATTERNS ? hdr[3] : 0;
    for (auto& p : tr.patterns) {
      uint8_t len = 16;
      uint16_t count = 0;
      if (f.read(&len, 1) != 1 || f.read((uint8_t*)&count, 2) != 2) break;
      p.length = (len >= 1 && len <= MAX_STEPS) ? len : 16;
      p.notes.clear();
      p.notes.reserve(count);
      for (uint16_t i = 0; i < count; i++) {
        uint8_t rec[6];
        if (f.read(rec, 6) != 6) break;
        NoteEvent e{rec[0], (int8_t)rec[1], rec[2], rec[3], (uint16_t)(rec[4] | (rec[5] << 8)), 0};
        if (e.step < p.length && e.pitch < 128) p.notes.push_back(e);
      }
      std::sort(p.notes.begin(), p.notes.end(), lessEv);
    }
  }
  f.close();
  return true;
}

}  // namespace seq
