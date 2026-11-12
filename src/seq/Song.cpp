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

// ── Instruments ──────────────────────────────────────────────────────────────
struct PresetDef {
  const char* name;
  Instrument in;
};
//                       snd vol  wave           oct det  A   D   S   R   cut res env
static const PresetDef PRESETS[PRESET_COUNT] = {
    {"Bass",   {1, 110, WAVE_SAW,      0,  0,  0, 30, 60, 10, 38, 50, 55}},
    {"Pad",    {1,  80, WAVE_SAW,      0, 40, 70, 70, 90, 80, 62, 15, 20}},
    {"Lead",   {1,  90, WAVE_SQUARE,   0, 10,  2, 40, 90, 25, 75, 35, 30}},
    {"Pluck",  {1, 100, WAVE_PULSE,    0,  0,  0, 28,  0, 25, 45, 40, 80}},
    {"Keys",   {1,  95, WAVE_TRIANGLE, 0,  6,  0, 50, 60, 40, 90, 10, 20}},
    {"Bell",   {1,  85, WAVE_SINE,     1,  0,  0, 70,  0, 70, 127, 0,  0}},
    {"Organ",  {1,  80, WAVE_SQUARE,   0, 12,  4, 10, 127, 20, 70, 10,  0}},
    {"Sub",    {1, 120, WAVE_SINE,    -1,  0,  0, 20, 110, 15, 127, 0,  0}},
};

const char* presetName(uint8_t p) { return p < PRESET_COUNT ? PRESETS[p].name : ""; }

void applyPreset(Instrument& in, uint8_t p) {
  if (p < PRESET_COUNT) in = PRESETS[p].in;
}

void defaultInstrument(Instrument& in, uint8_t track, bool drum) {
  if (drum) {
    in = Instrument{1, 110, WAVE_NOISE, 0, 0, 0, 64, 0, 0, 80, 0, 0};
    return;
  }
  static const uint8_t BY_TRACK[TRACKS] = {0, 0, 1, 2, 3, 4, 5, 6};  // BASS, CHRD=Pad, LEAD...
  applyPreset(in, BY_TRACK[track % TRACKS]);
}

// ── Drum MIDI routing ────────────────────────────────────────────────────────
const uint8_t DRUM_LANE_ID[DRUM_LANES] = {36, 38, 37, 39, 42, 46, 41, 45, 48, 49, 51, 56};

int drumLaneOf(uint8_t pitch) {
  for (uint8_t i = 0; i < DRUM_LANES; i++)
    if (DRUM_LANE_ID[i] == pitch) return i;
  return -1;
}

void drumRoutingGmKit(DrumRouting& r) {
  r.mode = DRUM_KIT;
  memcpy(r.kitNote, DRUM_LANE_ID, DRUM_LANES);
  // Keep the per-lane setup intact so switching back and forth loses nothing.
  bool unset = true;
  for (auto c : r.laneCh) unset &= c == 0;
  if (unset) {
    for (uint8_t i = 0; i < DRUM_LANES; i++) {
      r.laneCh[i] = (uint8_t)(i % 16 + 1);
      r.laneNote[i] = 60;
    }
  }
}

void drumRoutingPerLane(DrumRouting& r, uint8_t firstCh, uint8_t note) {
  r.mode = DRUM_LANES_MODE;
  for (uint8_t i = 0; i < DRUM_LANES; i++) {
    r.laneCh[i] = (uint8_t)((firstCh - 1 + i) % 16 + 1);
    r.laneNote[i] = note;
  }
}

void midiOut(const Track& tr, uint8_t pitch, uint8_t* ch, uint8_t* note) {
  *ch = tr.channel;
  *note = pitch;
  if (tr.kind != TrackKind::Drum) return;
  int lane = drumLaneOf(pitch);
  if (lane < 0) return;
  if (tr.drums.mode == DRUM_LANES_MODE) {
    *ch = tr.drums.laneCh[lane];
    *note = tr.drums.laneNote[lane];
  } else {
    *note = tr.drums.kitNote[lane];
  }
}

void songInitInto(Song& s) {
  s.bpm = 120;
  for (auto& sc : s.scenes)
    for (auto& pt : sc.pattern) pt = 0;
  s.arrange.assign(1, 0);
  s.songMode = false;
  for (uint8_t t = 0; t < TRACKS; t++) {
    Track& tr = s.tracks[t];
    strncpy(tr.name, DEFAULTS[t].name, sizeof(tr.name) - 1);
    tr.name[sizeof(tr.name) - 1] = 0;
    tr.kind = DEFAULTS[t].kind;
    tr.channel = DEFAULTS[t].channel;
    tr.mute = false;
    tr.hold = false;
    defaultInstrument(tr.inst, t, tr.kind == TrackKind::Drum);
    memset(&tr.drums, 0, sizeof(tr.drums));
    drumRoutingGmKit(tr.drums);
    tr.pattern = 0;
    for (auto& p : tr.patterns) {
      p.length = 16;
      p.notes.clear();
      p.fx.clear();
    }
  }
}

void songInit() { songInitInto(song); }

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
// Songs live in /songs/<name>.cms on the flash (LittleFS) partition, or in
// /cardputer-midi/songs/<name>.cms on the SD card.
// File layout (little endian):
//   "CMSG" u8 version  u16 bpm
//   per track: name[6] kind channel mute pattern [hold: v2+] [v7+: u8 patternCount]
//     per pattern: length u16 count, then count x {step offset pitch vel u16 len}
//                  [v3+: u16 fxCount, then fxCount x {step cmd val}]
//   [v4+: scenes SCENES x TRACKS bytes, u8 arrangeCount, arrange bytes, u8 songMode]
//   [v5+: per track, sizeof(Instrument) bytes]
//   [v6+: per track, sizeof(DrumRouting) bytes]
static constexpr uint8_t SONG_VERSION = 7;  // v7: pattern count per track; v6 is still written when 8 are enough
static char currentName[SONG_NAME_MAX + 1] = "";
static Storage currentStore = Storage::Flash;

// Songs folder on each storage; the SD folder is easy to find on a computer.
static const char* dirFor(Storage st) { return st == Storage::Sd ? "/cardputer-midi/songs" : "/songs"; }

static bool flashReady = false;

// Mounts the storage (and makes the songs folder) on first use.
static fs::FS* fsFor(Storage st) {
  if (st == Storage::Sd) {
    fs::FS* sd = sdFs();
    if (!sd) return nullptr;
    if (!sd->exists("/cardputer-midi")) sd->mkdir("/cardputer-midi");
    if (!sd->exists(dirFor(st))) sd->mkdir(dirFor(st));
    return sd;
  }
  if (!flashReady) {
    flashReady = LittleFS.begin(true);  // formats the partition on first use
    if (!flashReady) return nullptr;
    if (!LittleFS.exists("/songs")) LittleFS.mkdir("/songs");
    // Phase 2 kept a single song at /song.cms: move it into the song list.
    if (LittleFS.exists("/song.cms") && !LittleFS.exists("/songs/untitled.cms"))
      LittleFS.rename("/song.cms", "/songs/untitled.cms");
  }
  return &LittleFS;
}

bool storageAvailable(Storage st) { return fsFor(st) != nullptr; }
Storage currentSongStorage() { return currentStore; }

static String pathFor(Storage st, const char* name) {
  return String(dirFor(st)) + "/" + name + ".cms";
}

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

static void setCurrent(const char* name, Storage st) {
  strncpy(currentName, name, SONG_NAME_MAX);
  currentName[SONG_NAME_MAX] = 0;
  currentStore = st;
}

// True if the song uses any pattern beyond the 8 older versions could hold.
static bool usesExtraPatterns(const Song& s) {
  for (const auto& tr : s.tracks) {
    if (tr.pattern >= LEGACY_PATTERNS && tr.pattern < PATTERNS) return true;
    for (uint8_t i = LEGACY_PATTERNS; i < PATTERNS; i++) {
      const Pattern& p = tr.patterns[i];
      if (!p.notes.empty() || !p.fx.empty() || p.length != 16) return true;
    }
  }
  for (const auto& sc : s.scenes)
    for (uint8_t v : sc.pattern)
      if (v >= LEGACY_PATTERNS && v < PATTERNS) return true;
  return false;
}

static bool writeSong(File& f, const Song& s) {
  // Songs that fit in 8 patterns per track are written exactly as before, so
  // older firmware can still read them.
  const uint8_t ver = usesExtraPatterns(s) ? SONG_VERSION : 6;
  const uint8_t perTrack = ver >= 7 ? PATTERNS : LEGACY_PATTERNS;
  f.write((const uint8_t*)"CMSG", 4);
  f.write(ver);
  f.write((const uint8_t*)&s.bpm, 2);
  for (const auto& tr : s.tracks) {
    f.write((const uint8_t*)tr.name, 6);
    f.write((uint8_t)tr.kind);
    f.write(tr.channel);
    f.write((uint8_t)tr.mute);
    f.write(tr.pattern);
    f.write((uint8_t)tr.hold);
    if (ver >= 7) f.write(perTrack);
    for (uint8_t pi = 0; pi < perTrack; pi++) {
      const Pattern& p = tr.patterns[pi];
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
  for (const auto& sc : s.scenes) f.write(sc.pattern, TRACKS);
  f.write((uint8_t)s.arrange.size());
  if (!s.arrange.empty()) f.write(s.arrange.data(), s.arrange.size());
  f.write((uint8_t)s.songMode);
  static_assert(sizeof(Instrument) == 12, "song file stores Instrument as 12 bytes");
  for (const auto& tr : s.tracks) f.write((const uint8_t*)&tr.inst, sizeof(Instrument));
  static_assert(sizeof(DrumRouting) == 37, "song file stores DrumRouting as 37 bytes");
  for (const auto& tr : s.tracks) f.write((const uint8_t*)&tr.drums, sizeof(DrumRouting));
  return f.getWriteError() == 0;
}

static bool readSong(File& f, Song& s) {
  char magic[4];
  uint8_t ver = 0;
  uint16_t bpm = 0;
  bool ok = f.read((uint8_t*)magic, 4) == 4 && memcmp(magic, "CMSG", 4) == 0 &&
            f.read(&ver, 1) == 1 && ver >= 1 && ver <= SONG_VERSION &&
            f.read((uint8_t*)&bpm, 2) == 2;
  if (!ok) return false;
  songInitInto(s);
  s.bpm = (bpm >= BPM_MIN && bpm <= BPM_MAX) ? bpm : 120;
  for (auto& tr : s.tracks) {
    uint8_t hdr[4];
    if (f.read((uint8_t*)tr.name, 6) != 6 || f.read(hdr, 4) != 4) return false;
    tr.name[5] = 0;
    tr.kind = hdr[0] == 1 ? TrackKind::Drum : TrackKind::Synth;
    if (ver < 5) defaultInstrument(tr.inst, (uint8_t)(&tr - s.tracks), tr.kind == TrackKind::Drum);
    tr.channel = (hdr[1] >= 1 && hdr[1] <= 16) ? hdr[1] : 1;
    tr.mute = hdr[2] != 0;
    tr.pattern = hdr[3] < PATTERNS ? hdr[3] : 0;
    uint8_t hold = 0;
    if (ver >= 2 && f.read(&hold, 1) != 1) return false;
    tr.hold = hold != 0;
    uint8_t perTrack = LEGACY_PATTERNS;
    if (ver >= 7 && (f.read(&perTrack, 1) != 1 || perTrack == 0 || perTrack > 64)) return false;
    for (uint8_t pi = 0; pi < perTrack; pi++) {
      Pattern scratch;  // patterns past what this firmware holds are read and dropped
      Pattern& p = pi < PATTERNS ? tr.patterns[pi] : scratch;
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
    for (auto& sc : s.scenes) {
      if (f.read(sc.pattern, TRACKS) != TRACKS) return false;
      for (auto& pt : sc.pattern)
        if (pt >= PATTERNS) pt = SCENE_SILENT;
    }
    uint8_t n = 0, mode = 0;
    if (f.read(&n, 1) != 1) return false;
    s.arrange.resize(n);
    if (n && f.read(s.arrange.data(), n) != n) return false;
    for (auto& a : s.arrange)
      if (a >= SCENES) a = 0;
    if (s.arrange.empty()) s.arrange.assign(1, 0);
    if (f.read(&mode, 1) != 1) return false;
    s.songMode = mode != 0;
  }
  if (ver >= 5) {
    for (auto& tr : s.tracks) {
      if (f.read((uint8_t*)&tr.inst, sizeof(Instrument)) != sizeof(Instrument)) return false;
      if (tr.inst.wave >= WAVE_COUNT) tr.inst.wave = WAVE_SAW;
    }
  }
  if (ver >= 6) {
    for (auto& tr : s.tracks) {
      if (f.read((uint8_t*)&tr.drums, sizeof(DrumRouting)) != sizeof(DrumRouting)) return false;
      if (tr.drums.mode > DRUM_LANES_MODE) tr.drums.mode = DRUM_KIT;
      for (uint8_t i = 0; i < DRUM_LANES; i++) {
        if (tr.drums.laneCh[i] < 1 || tr.drums.laneCh[i] > 16) tr.drums.laneCh[i] = (uint8_t)(i % 16 + 1);
        tr.drums.kitNote[i] &= 0x7F;
        tr.drums.laneNote[i] &= 0x7F;
      }
    }
  }
  return true;
}

bool songSaveAs(const char* name, Storage st) {
  fs::FS* fsys = fsFor(st);
  if (!validSongName(name) || !fsys) return false;
  // Write to a temp file first so a failed save never destroys the old one.
  String tmp = String(dirFor(st)) + "/_tmp.cms_";
  File f = fsys->open(tmp, "w");
  if (!f) return false;
  bool ok = writeSong(f, song);
  f.close();
  if (!ok) {
    fsys->remove(tmp);
    return false;
  }
  String path = pathFor(st, name);
  fsys->remove(path);
  if (!fsys->rename(tmp, path)) return false;
  setCurrent(name, st);
  return true;
}

// Same file format, but for a song held in its own object: the sync can read
// and write songs without touching the one that is open for editing.
bool songSaveCopy(const char* name, const Song& s, Storage st) {
  fs::FS* fsys = fsFor(st);
  if (!validSongName(name) || !fsys) return false;
  String tmp = String(dirFor(st)) + "/_tmp.cms_";
  File f = fsys->open(tmp, "w");
  if (!f) return false;
  bool ok = writeSong(f, s);
  f.close();
  if (!ok) {
    fsys->remove(tmp);
    return false;
  }
  String path = pathFor(st, name);
  fsys->remove(path);
  return fsys->rename(tmp, path);
}

bool songLoadCopy(const char* name, Song& out, Storage st) {
  fs::FS* fsys = fsFor(st);
  if (!validSongName(name) || !fsys) return false;
  File f = fsys->open(pathFor(st, name), "r");
  if (!f) return false;
  bool ok = readSong(f, out);
  f.close();
  return ok;
}

bool songSave() { return currentName[0] && songSaveAs(currentName, currentStore); }

bool songLoadNamed(const char* name, Storage st) {
  fs::FS* fsys = fsFor(st);
  if (!validSongName(name) || !fsys) return false;
  File f = fsys->open(pathFor(st, name), "r");
  if (!f) return false;
  bool ok = readSong(f, song);
  f.close();
  if (!ok) {
    songInit();  // never leave a half-loaded song behind
    return false;
  }
  setCurrent(name, st);
  return true;
}

bool songDelete(const char* name, Storage st) {
  fs::FS* fsys = fsFor(st);
  if (!validSongName(name) || !fsys) return false;
  bool ok = fsys->remove(pathFor(st, name));
  if (ok && st == currentStore && strcmp(name, currentName) == 0) currentName[0] = 0;
  return ok;
}

bool songCopy(const char* name, Storage from, Storage to) {
  fs::FS* src = fsFor(from);
  fs::FS* dst = fsFor(to);
  if (!validSongName(name) || !src || !dst || from == to) return false;
  File in = src->open(pathFor(from, name), "r");
  if (!in) return false;
  String tmp = String(dirFor(to)) + "/_tmp.cms_";
  File out = dst->open(tmp, "w");
  if (!out) {
    in.close();
    return false;
  }
  uint8_t buf[256];
  bool ok = true;
  for (;;) {
    int n = in.read(buf, sizeof(buf));
    if (n <= 0) break;
    if (out.write(buf, (size_t)n) != (size_t)n) {
      ok = false;
      break;
    }
  }
  in.close();
  out.close();
  if (!ok) {
    dst->remove(tmp);
    return false;
  }
  String path = pathFor(to, name);
  dst->remove(path);
  return dst->rename(tmp, path);
}

void songNew() {
  songInit();
  currentName[0] = 0;
}

std::vector<String> songList(Storage st) {
  std::vector<String> names;
  fs::FS* fsys = fsFor(st);
  if (!fsys) return names;
  File dir = fsys->open(dirFor(st));
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
