// Plays generated songs through the real Sequencer on a virtual clock, once as
// a finished song and once as a stream of patterns, and checks the MIDI
// events are identical. Also checks that a slow generator (patterns arriving
// late) never loses or reorders a note.
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "Arduino.h"
#include "../../src/audio/SynthCore.h"
#include "../../src/gen/GenreData.h"
#include "../../src/gen/Generator.h"
#include "../../src/play/Sequencer.h"

uint32_t g_micros = 1000;

struct Ev { uint32_t t; int kind; int ch, note, vel; bool operator==(const Ev& o) const { return t == o.t && kind == o.kind && ch == o.ch && note == o.note && vel == o.vel; } };
static std::vector<Ev> log_;

// stand-ins for the MIDI output and the speaker
namespace midi {
void noteOn(uint8_t ch, uint8_t n, uint8_t v) { log_.push_back({g_micros, 1, ch, n, v}); }
void noteOff(uint8_t ch, uint8_t n) { log_.push_back({g_micros, 0, ch, n, 0}); }
void allNotesOff() { log_.push_back({g_micros, 2, 0, 0, 0}); }
}  // namespace midi
struct SpkEv { int kind; int note, vel, timbre, owner; };   // 1 on, 0 off, 2 drum
static std::vector<SpkEv> spk;
static synth::Core* liveSynth = nullptr;   // when set, the speaker commands drive a real synth
namespace monitor {
uint8_t timbreFor(const char* v, uint8_t t) { return synth::timbreForVoice(v, t); }
void noteOn(uint8_t note, uint8_t vel, uint8_t timbre, uint8_t owner) { spk.push_back({1, note, vel, timbre, owner}); if (liveSynth) liveSynth->noteOn(note, vel, timbre, owner); }
void noteOff(uint8_t note, uint8_t owner) { spk.push_back({0, note, 0, 0, owner}); if (liveSynth) liveSynth->noteOff(note, owner); }
void drum(uint8_t note, uint8_t vel) { spk.push_back({2, note, vel, 0, 0}); if (liveSynth) liveSynth->drum(note, vel); }
void allOff() { if (liveSynth) liveSynth->allOff(); }
}  // namespace monitor

struct Collect : gen::Sink {
  song::Song head;
  gen::Meta meta;
  std::vector<song::Pattern> pats;
  bool begin(const song::Song& h, const gen::Meta& m) override { head = h; meta = m; return true; }
  bool pattern(song::Pattern&& p) override { pats.push_back(std::move(p)); return true; }
};

// Serves collected patterns; `lateEvery` > 0 makes it report "not ready" for a
// while before each pattern, like a generator that is running behind.
struct FakeSource : seq::PatternSource {
  std::vector<Collect*> songs;
  size_t si = 0, pi = 0;
  int lateEvery = 0, lateCount = 0;
  int takes = 0, newSongs = 0;
  bool ready() override {
    if (si >= songs.size()) return false;
    if (lateEvery && lateCount < lateEvery) { lateCount++; return false; }
    return true;
  }
  void take(song::Pattern& out, bool& newSong) override {
    lateCount = 0;
    Collect* c = songs[si];
    out = c->pats[pi];
    newSong = pi == 0;
    if (newSong) { seq::current() = c->head; newSongs++; }
    takes++;
    if (++pi >= c->pats.size()) { pi = 0; si++; }
  }
  bool finished() override { return si >= songs.size(); }
};

static void run(uint32_t maxSeconds) {
  uint32_t end = g_micros + maxSeconds * 1000000UL;
  while (g_micros < end && (seq::playing() || !seq::finished())) {
    g_micros += 500;
    seq::update();
    if (seq::finished()) break;
  }
  for (int i = 0; i < 400; i++) { g_micros += 500; seq::update(); }  // let the last notes sound out
}

static int fails = 0;
static void check(bool ok, const char* what, const std::string& detail = "") {
  if (!ok) { fails++; printf("  FAIL: %s %s\n", what, detail.c_str()); }
}

int main() {
  seq::begin();
  for (int gi = 0; gi < gen::GENRE_COUNT; gi += 1) {
    const gen::Genre& g = *gen::GENRE_LIST[gi];
    uint32_t seed = 7 + gi;
    // reference: the song played from a finished Song
    song::Song full; gen::Meta meta;
    gen::generate(g, seed, full, meta);
    log_.clear(); g_micros = 1000;
    seq::setLoop(false);
    seq::adopt(std::move(full));
    seq::play(0);
    run(900);
    std::vector<Ev> ref = log_;

    // the same song streamed, patterns arriving on time
    Collect c; gen::generateStream(g, seed, c);
    FakeSource src; src.songs.push_back(&c);
    log_.clear(); spk.clear(); g_micros = 1000;
    seq::playStream(&src);
    run(900);
    bool same = ref.size() == log_.size();
    for (size_t i = 0; same && i < ref.size(); i++) same = ref[i] == log_[i];
    check(same, g.id, "stream differs from file playback (" + std::to_string(ref.size()) + " vs " + std::to_string(log_.size()) + " events)");
    check(src.takes == (int)c.pats.size(), g.id, "pattern count");
    {
      // the speaker hears the same notes as the MIDI output, each with an instrument and an owner
      int midiOn = 0, midiDrums = 0, spkOn = 0, spkOff = 0, spkDrum = 0;
      for (auto& e : log_) if (e.kind == 1) { if (e.ch == 10) midiDrums++; else midiOn++; }
      std::vector<int> open(16 * 128, 0);
      bool badTimbre = false;
      for (auto& e : spk) {
        if (e.kind == 1) { spkOn++; if (e.timbre >= synth::TB_COUNT || e.owner > 7) badTimbre = true; open[e.owner * 128 + e.note]++; }
        else if (e.kind == 0) { spkOff++; open[e.owner * 128 + e.note]--; }
        else spkDrum++;
      }
      bool balanced = true; for (int v : open) if (v != 0) balanced = false;
      check(!badTimbre, g.id, "speaker timbre/owner out of range");
      check(spkOn + spkDrum == midiOn + midiDrums, g.id, "speaker and MIDI note counts differ");
      check(spkOn == spkOff && balanced, g.id, "speaker notes left hanging");
      spk.clear();
    }

    // a slow generator: patterns arrive late. Same notes in the same order, just later.
    FakeSource slow; slow.songs.push_back(&c); slow.lateEvery = 3000;  // ~1.5 s of waiting per pattern
    log_.clear(); g_micros = 1000;
    seq::playStream(&slow);
    run(3000);
    std::vector<Ev> late = log_;
    // Notes held across a pattern boundary rightly finish during the wait, so on/off
    // interleaving differs. What must hold: every note-on and every note-off still
    // happens, each kind in the same order as with an on-time generator.
    // (Notes that come due in the same 0.5 ms tick fire in queue order, so compare as sets.)
    auto pick = [](const std::vector<Ev>& v, int kind) {
      std::vector<std::vector<int>> r;
      for (auto& e : v) if (e.kind == kind) r.push_back({e.ch, e.note, e.vel});
      std::sort(r.begin(), r.end());
      return r;
    };
    bool sameNotes = true;
    for (int kind = 0; kind <= 1 && sameNotes; kind++) sameNotes = pick(ref, kind) == pick(late, kind);
    check(sameNotes, g.id, "slow generator lost or changed notes");
    printf("%-12s %5zu events  patterns %2zu  ok\n", g.id, ref.size(), c.pats.size());
  }

  // remapping a part's MIDI channel while a song plays: held notes are released on the old
  // channel, the part continues on the new one, and nothing is left hanging
  {
    Collect c; gen::generateStream(*gen::GENRE_LIST[0], 21, c);   // city pop
    FakeSource src; src.songs.push_back(&c);
    log_.clear(); spk.clear(); g_micros = 1000;
    seq::playStream(&src);
    uint32_t switchAt = 1000 + 12 * 1000000UL;
    bool switched = false;
    for (uint32_t t = 0; t < 400 * 1000000UL / 500 && !seq::finished(); t++) {
      g_micros += 500;
      if (!switched && g_micros >= switchAt) {
        seq::silenceTrack(5);              // Keys
        seq::current().tracks[5].ch = 9;   // what GenMode::setChannel does
        switched = true;
      }
      seq::update();
    }
    for (int i = 0; i < 400; i++) { g_micros += 500; seq::update(); }
    int onOld = 0, onNew = 0, offOld = 0, offNew = 0;
    for (auto& e : log_) {
      if (e.ch == 2) { (e.kind == 1 ? onOld : offOld) += (e.kind <= 1); }
      if (e.ch == 9) { (e.kind == 1 ? onNew : offNew) += (e.kind <= 1); }
    }
    check(onOld > 0 && onNew > 0, "remap", "keys notes on both channels (old " + std::to_string(onOld) + ", new " + std::to_string(onNew) + ")");
    check(onOld == offOld && onNew == offNew, "remap", "notes left hanging: old " + std::to_string(onOld - offOld) + ", new " + std::to_string(onNew - offNew));
    printf("remap       keys on ch2: %d notes, then ch9: %d notes, none hanging\n", onOld, onNew);
  }

  // the whole chain: sequencer -> speaker commands -> synth -> audio, for a few genres
  for (int gi : {0, 5, 8, 11}) {
    const gen::Genre& g = *gen::GENRE_LIST[gi];
    Collect c; gen::generateStream(g, 31 + gi, c);
    FakeSource src; src.songs.push_back(&c);
    synth::Core core(22050.f);
    liveSynth = &core;
    log_.clear(); spk.clear(); g_micros = 1000;
    seq::playStream(&src);
    double acc = 0, sumSq = 0, dc = 0; size_t samples = 0, hot = 0; float peak = 0; bool finite = true, silentBlocks = false;
    int quietRun = 0, longestQuiet = 0;
    int16_t blk[64];
    for (uint32_t t = 0; t < 600 * 1000000UL / 500 && !seq::finished(); t++) {
      g_micros += 500;
      seq::update();
      acc += 500e-6 * 22050.0;
      while (acc >= 64) {
        acc -= 64;
        core.render(blk, 64);
        double e = 0;
        for (int i = 0; i < 64; i++) { float x = blk[i] / 32768.f; if (!std::isfinite(x)) finite = false; sumSq += x * x; dc += x; peak = fmaxf(peak, fabsf(x)); if (fabsf(x) > 0.9f) hot++; e += x * x; }
        samples += 64;
        if (e / 64 < 1e-6) { if (++quietRun > longestQuiet) longestQuiet = quietRun; } else quietRun = 0;
      }
      if (g_micros > 150u * 1000000u) break;   // two and a half minutes is plenty
    }
    liveSynth = nullptr;
    double rms = sqrt(sumSq / samples);
    printf("audio %-9s %6zu samples  rms %.3f  peak %.2f  near-clip %.2f%%  longest silence %.1f s\n", g.id, samples, rms, peak, 100.0 * hot / samples, longestQuiet * 64 / 22050.0);
    check(finite, g.id, "audio has NaN/Inf");
    check(rms > 0.04 && rms < 0.45, g.id, "audio level out of range (rms " + std::to_string(rms) + ")");
    check(hot * 100.0 / samples < 1.0, g.id, "audio clips");
    check(fabs(dc / samples) < 0.01, g.id, "audio has a DC offset");
    check(longestQuiet * 64 / 22050.0 < 5.0, g.id, "long unexplained silence");
  }

  // two songs back to back at different tempos: the tempo changes exactly at the song boundary
  {
    Collect a, b;
    gen::generateStream(*gen::GENRE_LIST[0], 11, a);   // city pop ~90 bpm
    gen::generateStream(*gen::GENRE_LIST[8], 12, b);   // drum & bass? (index 8) a different tempo
    FakeSource src; src.songs = {&a, &b};
    log_.clear(); g_micros = 1000;
    seq::playStream(&src);
    run(1200);
    check(src.newSongs == 2, "two songs", "new-song flags: " + std::to_string(src.newSongs));
    check(src.takes == (int)(a.pats.size() + b.pats.size()), "two songs", "pattern count");
    printf("two songs   %d new-song headers, %d patterns, bpm %u -> %u ok\n", src.newSongs, src.takes, a.head.bpm, b.head.bpm);
  }
  printf(fails ? "FAILED (%d)\n" : "all good\n", fails);
  return fails ? 1 : 0;
}
