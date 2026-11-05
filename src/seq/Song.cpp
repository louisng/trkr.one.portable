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
  for (auto& sc : song.scenes)
    for (auto& pt : sc.pattern) pt = 0;
  song.arrange.assign(1, 0);
  song.songMode = false;
  for (uint8_t t = 0; t < TRACKS; t++) {
    Track& tr = song.tracks[t];
    strncpy(tr.name, DEFAULTS[t].name, sizeof(tr.name) - 1);
    tr.name[sizeof(tr.name) - 1] = 0;
    tr.kind = DEFAULTS[t].kind;
    tr.channel = DEFAULTS[t].channel;
    tr.mute = false;
    tr.hold = false;
    tr.pattern = 0;
    for (auto& p : tr.patterns) {
      p.length = 16;
      p.notes.clear();
      p.fx.clear();
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

void setStepLength(Pattern& p, uint8_t step, uint16_t lenTicks) {
  for (auto& e : p.notes)
    if (e.step == step && e.pitch != PITCH_OFF) e.len = lenTicks ? lenTicks : 1;
}

void setStepVelocity(Pattern& p, uint8_t step, uint8_t vel) {
  if (vel < 1) vel = 1;
  if (vel > 127) vel = 127;
  for (auto& e : p.notes)
    if (e.step == step && e.pitch != PITCH_OFF) e.vel = vel;
}

void setStepOffset(Pattern& p, uint8_t step, int8_t offset) {
  const int8_t lo = -(int8_t)(TICKS_PER_STEP / 2), hi = (int8_t)(TICKS_PER_STEP / 2 - 1);
  if (offset < lo) offset = lo;
  if (offset > hi) offset = hi;
  for (auto& e : p.notes)
    if (e.step == step) e.offset = offset;
}

bool removeNote(Pattern& p, uint8_t step, uint8_t pitch) {
  for (auto it = p.notes.begin(); it != p.notes.end(); ++it)
    if (it->step == step && it->pitch == pitch) {
      p.notes.erase(it);
      return true;
    }
  return false;
}

bool repitchNote(Pattern& p, uint8_t step, uint8_t oldPitch, uint8_t newPitch) {
  if (newPitch > 127 || hasNote(p, step, newPitch)) return false;
  for (auto it = p.notes.begin(); it != p.notes.end(); ++it)
    if (it->step == step && it->pitch == oldPitch) {
      NoteEvent e = *it;
      p.notes.erase(it);
      e.pitch = newPitch;
      return addNote(p, e);
    }
  return false;
}

uint16_t sceneSteps(uint8_t scene) {
  uint16_t len = 0;
  if (scene >= SCENES) return 16;
  for (uint8_t t = 0; t < TRACKS; t++) {
    uint8_t pt = song.scenes[scene].pattern[t];
    if (pt < PATTERNS && song.tracks[t].patterns[pt].length > len)
      len = song.tracks[t].patterns[pt].length;
  }
  return len ? len : 16;
}

bool fxAt(const Pattern& p, uint8_t step, uint8_t* cmd, uint8_t* val) {
  for (const auto& x : p.fx)
    if (x.step == step) {
      if (cmd) *cmd = x.cmd;
      if (val) *val = x.val;
      return true;
    }
  return false;
}

void setFx(Pattern& p, uint8_t step, uint8_t cmd, uint8_t val) {
  for (auto it = p.fx.begin(); it != p.fx.end(); ++it)
    if (it->step == step) {
      if (cmd == FX_NONE) p.fx.erase(it);
      else {
        it->cmd = cmd;
        it->val = val;
      }
      return;
    }
  if (cmd != FX_NONE && step < p.length) p.fx.push_back(StepFx{step, cmd, val});
}

bool doublePattern(Pattern& p) {
  if ((int)p.length * 2 > MAX_STEPS) return false;
  const size_t nfx = p.fx.size();
  for (size_t i = 0; i < nfx; i++) {
    StepFx x = p.fx[i];
    x.step += p.length;
    p.fx.push_back(x);
  }
  const uint8_t len = p.length;
  const size_t n = p.notes.size();
  for (size_t i = 0; i < n; i++) {
    NoteEvent e = p.notes[i];
    e.step += len;
    e.flags = 0;
    p.notes.push_back(e);
  }
  p.length = len * 2;
  std::sort(p.notes.begin(), p.notes.end(), lessEv);
  return true;
}

void setStepOff(Pattern& p, uint8_t step) {
  clearStep(p, step);
  if (step < p.length) addNote(p, NoteEvent{step, 0, PITCH_OFF, 0, 1, 0});
}

bool isOffStep(const Pattern& p, uint8_t step) { return hasNote(p, step, PITCH_OFF); }

void setPatternLength(Pattern& p, uint8_t length) {
  if (length < 1) length = 1;
  if (length > MAX_STEPS) length = MAX_STEPS;
  p.length = length;
  p.fx.erase(std::remove_if(p.fx.begin(), p.fx.end(),
                            [length](const StepFx& x) { return x.step >= length; }),
             p.fx.end());
  p.notes.erase(std::remove_if(p.notes.begin(), p.notes.end(),
                               [length](const NoteEvent& e) { return e.step >= length; }),
                p.notes.end());
}

// ── Persistence ──────────────────────────────────────────────────────────────
// Songs live in /songs/<name>.cms on the LittleFS partition.
// File layout (little endian):
//   "CMSG" u8 version  u16 bpm
//   per track: name[6] kind channel mute pattern [hold: v2+]
//     per pattern: length u16 count, then count x {step offset pitch vel u16 len}
//                  [v3+: u16 fxCount, then fxCount x {step cmd val}]
//   [v4+: scenes SCENES x TRACKS bytes, u8 arrangeCount, arrange bytes, u8 songMode]
static const char* SONG_DIR = "/songs";
static constexpr uint8_t SONG_VERSION = 4;
static bool fsReady = false;
static char currentName[SONG_NAME_MAX + 1] = "";

static bool ensureFs() {
  if (fsReady) return true;
  fsReady = LittleFS.begin(true);  // formats the partition on first use
  if (!fsReady) return false;
  if (!LittleFS.exists(SONG_DIR)) LittleFS.mkdir(SONG_DIR);
  // Phase 2 kept a single song at /song.cms: move it into the song list.
  if (LittleFS.exists("/song.cms") && !LittleFS.exists("/songs/untitled.cms"))
    LittleFS.rename("/song.cms", "/songs/untitled.cms");
  return true;
}

static String pathFor(const char* name) { return String(SONG_DIR) + "/" + name + ".cms"; }

bool validSongName(const char* name) {
  size_t n = strlen(name);
  if (n == 0 || n > SONG_NAME_MAX) return false;
  for (size_t i = 0; i < n; i++) {
    char c = name[i];
    if (!(isalnum((unsigned char)c) || c == '-' || c == '_' || c == ' ')) return false;
  }
  return name[0] != ' ' && name[n - 1] != ' ';
}

const char* currentSongName() { return currentName; }

static void setCurrentName(const char* name) {
  strncpy(currentName, name, SONG_NAME_MAX);
  currentName[SONG_NAME_MAX] = 0;
}

static bool writeSong(File& f) {
  f.write((const uint8_t*)"CMSG", 4);
  f.write(SONG_VERSION);
  f.write((const uint8_t*)&song.bpm, 2);
  for (const auto& tr : song.tracks) {
    f.write((const uint8_t*)tr.name, 6);
    f.write((uint8_t)tr.kind);
    f.write(tr.channel);
    f.write((uint8_t)tr.mute);
    f.write(tr.pattern);
    f.write((uint8_t)tr.hold);
    for (const auto& p : tr.patterns) {
      f.write(p.length);
      uint16_t count = (uint16_t)p.notes.size();
      f.write((const uint8_t*)&count, 2);
      for (const auto& e : p.notes) {
        uint8_t rec[6] = {e.step, (uint8_t)e.offset, e.pitch, e.vel, (uint8_t)(e.len & 0xFF),
                          (uint8_t)(e.len >> 8)};
        f.write(rec, 6);
      }
      uint16_t fxCount = (uint16_t)p.fx.size();
      f.write((const uint8_t*)&fxCount, 2);
      for (const auto& x : p.fx) {
        uint8_t rec[3] = {x.step, x.cmd, x.val};
        f.write(rec, 3);
      }
    }
  }
  for (const auto& sc : song.scenes) f.write(sc.pattern, TRACKS);
  f.write((uint8_t)song.arrange.size());
  if (!song.arrange.empty()) f.write(song.arrange.data(), song.arrange.size());
  f.write((uint8_t)song.songMode);
  return f.getWriteError() == 0;
}

static bool readSong(File& f) {
  char magic[4];
  uint8_t ver = 0;
  uint16_t bpm = 0;
  bool ok = f.read((uint8_t*)magic, 4) == 4 && memcmp(magic, "CMSG", 4) == 0 &&
            f.read(&ver, 1) == 1 && ver >= 1 && ver <= SONG_VERSION &&
            f.read((uint8_t*)&bpm, 2) == 2;
  if (!ok) return false;
  songInit();
  song.bpm = (bpm >= BPM_MIN && bpm <= BPM_MAX) ? bpm : 120;
  for (auto& tr : song.tracks) {
    uint8_t hdr[4];
    if (f.read((uint8_t*)tr.name, 6) != 6 || f.read(hdr, 4) != 4) return false;
    tr.name[5] = 0;
    tr.kind = hdr[0] == 1 ? TrackKind::Drum : TrackKind::Synth;
    tr.channel = (hdr[1] >= 1 && hdr[1] <= 16) ? hdr[1] : 1;
    tr.mute = hdr[2] != 0;
    tr.pattern = hdr[3] < PATTERNS ? hdr[3] : 0;
    uint8_t hold = 0;
    if (ver >= 2 && f.read(&hold, 1) != 1) return false;
    tr.hold = hold != 0;
    for (auto& p : tr.patterns) {
      uint8_t len = 16;
      uint16_t count = 0;
      if (f.read(&len, 1) != 1 || f.read((uint8_t*)&count, 2) != 2) return false;
      p.length = (len >= 1 && len <= MAX_STEPS) ? len : 16;
      p.notes.clear();
      p.notes.reserve(count);
      for (uint16_t i = 0; i < count; i++) {
        uint8_t rec[6];
        if (f.read(rec, 6) != 6) return false;
        NoteEvent e{rec[0], (int8_t)rec[1], rec[2], rec[3], (uint16_t)(rec[4] | (rec[5] << 8)), 0};
        if (e.step < p.length && (e.pitch < 128 || e.pitch == PITCH_OFF)) p.notes.push_back(e);
      }
      std::sort(p.notes.begin(), p.notes.end(), lessEv);
      p.fx.clear();
      if (ver >= 3) {
        uint16_t fxCount = 0;
        if (f.read((uint8_t*)&fxCount, 2) != 2) return false;
        for (uint16_t i = 0; i < fxCount; i++) {
          uint8_t rec[3];
          if (f.read(rec, 3) != 3) return false;
          if (rec[0] < p.length && rec[1] > FX_NONE && rec[1] < FX_COUNT) setFx(p, rec[0], rec[1], rec[2]);
        }
      }
    }
  }
  if (ver >= 4) {
    for (auto& sc : song.scenes) {
      if (f.read(sc.pattern, TRACKS) != TRACKS) return false;
      for (auto& pt : sc.pattern)
        if (pt >= PATTERNS) pt = SCENE_SILENT;
    }
    uint8_t n = 0, mode = 0;
    if (f.read(&n, 1) != 1) return false;
    song.arrange.resize(n);
    if (n && f.read(song.arrange.data(), n) != n) return false;
    for (auto& a : song.arrange)
      if (a >= SCENES) a = 0;
    if (song.arrange.empty()) song.arrange.assign(1, 0);
    if (f.read(&mode, 1) != 1) return false;
    song.songMode = mode != 0;
  }
  return true;
}

bool songSaveAs(const char* name) {
  if (!validSongName(name) || !ensureFs()) return false;
  String tmp = String(SONG_DIR) + "/.tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  bool ok = writeSong(f);
  f.close();
  if (!ok) {
    LittleFS.remove(tmp);
    return false;
  }
  String path = pathFor(name);
  LittleFS.remove(path);
  if (!LittleFS.rename(tmp, path)) return false;
  setCurrentName(name);
  return true;
}

bool songSave() { return currentName[0] && songSaveAs(currentName); }

bool songLoadNamed(const char* name) {
  if (!validSongName(name) || !ensureFs()) return false;
  File f = LittleFS.open(pathFor(name), "r");
  if (!f) return false;
  bool ok = readSong(f);
  f.close();
  if (!ok) {
    songInit();  // never leave a half-loaded song behind
    return false;
  }
  setCurrentName(name);
  return true;
}

bool songDelete(const char* name) {
  if (!validSongName(name) || !ensureFs()) return false;
  bool ok = LittleFS.remove(pathFor(name));
  if (ok && strcmp(name, currentName) == 0) currentName[0] = 0;
  return ok;
}

void songNew() {
  songInit();
  currentName[0] = 0;
}

std::vector<String> songList() {
  std::vector<String> names;
  if (!ensureFs()) return names;
  File dir = LittleFS.open(SONG_DIR);
  if (!dir) return names;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String n = f.name();
    int slash = n.lastIndexOf('/');
    if (slash >= 0) n = n.substring(slash + 1);
    if (n.endsWith(".cms")) names.push_back(n.substring(0, n.length() - 4));
    f.close();
  }
  std::sort(names.begin(), names.end(),
            [](const String& a, const String& b) { return a.compareTo(b) < 0; });
  return names;
}

}  // namespace seq
