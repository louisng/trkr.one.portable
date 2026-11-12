// Synth: edit the built-in sound of the selected track while you play it.
//
//   ; .    pick a setting          , /    change it (hold shift: x16)
//   f / l  previous / next track   keys   play, so you hear every change
#include <stdio.h>

#include "../audio/Synth.h"
#include "../input/KeyInput.h"
#include "../input/KeyLayout.h"
#include "../seq/Engine.h"
#include "Screen.h"

namespace ui {

using namespace seq;

static constexpr uint8_t IDX_SHIFT = 2 * 14 + 1;
static const char* const WAVE_NAMES[WAVE_COUNT] = {"Saw", "Square", "Pulse", "Triangle", "Sine", "Noise"};

enum Param : uint8_t {
  P_PRESET, P_SOUND, P_VOLUME, P_WAVE, P_OCTAVE, P_DETUNE,
  P_ATTACK, P_DECAY, P_SUSTAIN, P_RELEASE, P_CUTOFF, P_RES, P_ENVMOD, P_COUNT
};
static const char* const SYNTH_LABELS[P_COUNT] = {
    "Preset", "Sound", "Volume", "Wave", "Octave", "Detune", "Attack",
    "Decay",  "Sustain", "Release", "Cutoff", "Resonance", "Filter env"};

// Drum tracks only use a few of the same fields.
static const Param DRUM_PARAMS[] = {P_SOUND, P_VOLUME, P_CUTOFF, P_DECAY, P_OCTAVE};
static const char* drumLabel(Param p) {
  switch (p) {
    case P_CUTOFF: return "Tone";
    case P_DECAY: return "Decay";
    case P_OCTAVE: return "Pitch";
    default: return SYNTH_LABELS[p];
  }
}

static uint8_t clampU(int v, int lo, int hi) { return (uint8_t)(v < lo ? lo : v > hi ? hi : v); }

class SynthScreen : public Screen {
 public:
  const char* title() const override { return "SYNTH"; }

  void onAction(uint8_t a) override {
    const uint8_t t = selectedTrack();
    const bool drum = song.tracks[t].kind == TrackKind::Drum;
    const int count = drum ? (int)(sizeof(DRUM_PARAMS) / sizeof(DRUM_PARAMS[0])) : P_COUNT;
    if (sel >= count) sel = 0;
    switch (a) {
      case layout::ACT_UP: sel = (sel + count - 1) % count; break;
      case layout::ACT_DOWN: sel = (sel + 1) % count; break;
      case layout::ACT_LEFT:
      case layout::ACT_RIGHT: {
        const int dir = a == layout::ACT_RIGHT ? 1 : -1;
        change(song.tracks[t], drum ? DRUM_PARAMS[sel] : (Param)sel, dir,
               keys::isDown(IDX_SHIFT));
        break;
      }
      case layout::ACT_SHORTER:
        selectTrack((t + TRACKS - 1) % TRACKS);
        sel = 0;
        break;
      case layout::ACT_LONGER:
        selectTrack((t + 1) % TRACKS);
        sel = 0;
        break;
      default: break;
    }
  }

  bool animating() const override { return true; }  // CPU meter

  void draw(M5Canvas& c) override {
    const uint8_t t = selectedTrack();
    const Track& tr = song.tracks[t];
    const Instrument& in = tr.inst;
    const bool drum = tr.kind == TrackKind::Drum;
    const int count = drum ? (int)(sizeof(DRUM_PARAMS) / sizeof(DRUM_PARAMS[0])) : P_COUNT;
    if (sel >= count) sel = 0;

    c.setFont(&fonts::Font0);
    c.setTextDatum(top_left);
    c.setTextColor(col::HELD);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d %s  %s", t + 1, tr.name, drum ? "drum kit" : "synth");
    c.drawString(buf, 4, 16);
    c.setTextColor(col::DIM);
    c.setTextDatum(top_right);
    c.drawString("f/l track", W - 4, 16);

    const int rowH = 9, top = 27, listW = 134;
    const int visible = (H - top) / rowH;
    if (sel < first) first = sel;
    if (sel >= first + visible) first = sel - visible + 1;
    for (int i = first; i < count && i < first + visible; i++) {
      const Param p = drum ? DRUM_PARAMS[i] : (Param)i;
      const int y = top + (i - first) * rowH;
      if (i == sel) c.fillRect(0, y - 1, listW, rowH, col::SELECT);
      c.setTextDatum(top_left);
      c.setTextColor(i == sel ? col::TEXT : col::DIM);
      c.drawString(drum ? drumLabel(p) : SYNTH_LABELS[p], 4, y);
      value(in, p, drum, buf, sizeof(buf));
      c.setTextDatum(top_right);
      c.setTextColor(i == sel ? col::HELD : col::TEXT);
      c.drawString(buf, listW - 3, y);
    }

    if (!drum) drawEnvelope(c, in, listW + 6, 30, W - listW - 10, 50);
    c.setTextDatum(top_left);
    c.setTextColor(in.sound ? col::OK : col::FAINT);
    c.drawString(in.sound ? "built-in sound ON" : "MIDI only", listW + 6, 90);
    c.setTextColor(col::FAINT);
    snprintf(buf, sizeof(buf), "cpu %d%%", synth::load());
    c.drawString(buf, listW + 6, 102);
    c.drawString("play keys to hear", listW + 6, 114);
  }

 private:
  int sel = 0, first = 0;
  uint8_t preset = 0;

  void value(const Instrument& in, Param p, bool drum, char* out, size_t n) {
    switch (p) {
      case P_PRESET: snprintf(out, n, "< %s >", presetName(preset)); break;
      case P_SOUND: snprintf(out, n, "%s", in.sound ? "on" : "off"); break;
      case P_WAVE: snprintf(out, n, "%s", WAVE_NAMES[in.wave % WAVE_COUNT]); break;
      case P_OCTAVE: snprintf(out, n, "%+d", in.octave); break;
      case P_VOLUME: snprintf(out, n, "%d", in.volume); break;
      case P_DETUNE: snprintf(out, n, in.detune ? "%d" : "off", in.detune); break;
      case P_ATTACK: snprintf(out, n, "%d", in.attack); break;
      case P_DECAY: snprintf(out, n, "%d", in.decay); break;
      case P_SUSTAIN: snprintf(out, n, "%d", in.sustain); break;
      case P_RELEASE: snprintf(out, n, "%d", in.release); break;
      case P_CUTOFF: snprintf(out, n, "%d", in.cutoff); break;
      case P_RES: snprintf(out, n, "%d", in.resonance); break;
      case P_ENVMOD: snprintf(out, n, "%d", in.envMod); break;
      default: out[0] = 0;
    }
    (void)drum;
  }

  void change(Track& tr, Param p, int dir, bool big) {
    Instrument& in = tr.inst;
    const bool drum = tr.kind == TrackKind::Drum;
    const int d = dir * (big ? 16 : 1);
    switch (p) {
      case P_PRESET:
        preset = (uint8_t)((preset + PRESET_COUNT + dir) % PRESET_COUNT);
        applyPreset(in, preset);
        break;
      case P_SOUND:
        in.sound = !in.sound;
        if (!in.sound) synth::allOff();
        break;
      case P_WAVE: in.wave = (uint8_t)((in.wave + WAVE_COUNT + dir) % WAVE_COUNT); break;
      case P_OCTAVE: in.octave = (int8_t)(drum ? clampU(in.octave + 12 + dir, 0, 24) - 12
                                               : clampU(in.octave + 2 + dir, 0, 4) - 2);
        break;
      case P_VOLUME: in.volume = clampU(in.volume + d, 0, 127); break;
      case P_DETUNE: in.detune = clampU(in.detune + d, 0, 127); break;
      case P_ATTACK: in.attack = clampU(in.attack + d, 0, 127); break;
      case P_DECAY: in.decay = clampU(in.decay + d, 0, 127); break;
      case P_SUSTAIN: in.sustain = clampU(in.sustain + d, 0, 127); break;
      case P_RELEASE: in.release = clampU(in.release + d, 0, 127); break;
      case P_CUTOFF: in.cutoff = clampU(in.cutoff + d, 0, 127); break;
      case P_RES: in.resonance = clampU(in.resonance + d, 0, 127); break;
      case P_ENVMOD: in.envMod = clampU(in.envMod + d, 0, 127); break;
      default: break;
    }
  }

  // ADSR shape: attack, decay to sustain, a held stretch, then release.
  void drawEnvelope(M5Canvas& c, const Instrument& in, int x, int y, int w, int h) {
    c.drawRect(x, y, w, h, col::FAINT);
    const float seg = (w - 4) / 4.0f;
    auto len = [&](uint8_t v) { return 2.0f + (v / 127.0f) * (seg - 2.0f); };
    const float ax = x + 2 + len(in.attack);
    const float dx = ax + len(in.decay);
    const float sx = dx + seg * 0.6f;
    const float rx = sx + len(in.release);
    const int base = y + h - 3, peak = y + 3;
    const int sus = base - (int)((base - peak) * in.sustain / 127.0f);
    c.drawLine(x + 2, base, (int)ax, peak, col::ACCENT);
    c.drawLine((int)ax, peak, (int)dx, sus, col::ACCENT);
    c.drawLine((int)dx, sus, (int)sx, sus, col::ACCENT);
    c.drawLine((int)sx, sus, (int)rx, base, col::ACCENT);
  }
};

static SynthScreen instance;
Screen* synthScreen() { return &instance; }

}  // namespace ui
