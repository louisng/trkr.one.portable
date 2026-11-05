#include "Engine.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <math.h>

#include "../app/Settings.h"
#include "../audio/Monitor.h"
#include "../input/KeyLayout.h"
#include "../midi/MidiOut.h"
#include "../play/LivePlayer.h"

namespace seq {

static SemaphoreHandle_t mtx = nullptr;

void lock() { xSemaphoreTakeRecursive(mtx, portMAX_DELAY); }
void unlock() { xSemaphoreGiveRecursive(mtx); }

// ── State ────────────────────────────────────────────────────────────────────
static volatile bool isPlaying = false;
static bool recArmed = false;
static uint8_t sel = 1;  // BASS

static uint32_t nextTick = 0;      // next tick to process
static uint32_t nextTickUs = 0;    // when it is due
static uint32_t lastTickUs = 0;    // when the previous tick was processed

struct Active {
  bool used;
  uint8_t channel;
  uint8_t pitch;
  bool drum;
  uint32_t offTick;
};
static constexpr uint8_t MAX_ACTIVE = 64;
static Active active[MAX_ACTIVE];

struct RecHeld {
  bool used;
  uint8_t track;
  uint8_t pattern;
  uint8_t step;
  uint8_t count;
  uint8_t pitches[5];
  float startTick;
};
static RecHeld recHeld[layout::KEY_COUNT];

static uint32_t tickUs() { return (uint32_t)(60000000UL / ((uint32_t)song.bpm * TICKS_PER_STEP * 4)); }

// ── Track routing ────────────────────────────────────────────────────────────
uint8_t selectedTrack() { return sel; }
void selectTrack(uint8_t t) {
  if (t < TRACKS) sel = t;
}

static uint8_t firstOfKind(TrackKind k, uint8_t fallback) {
  for (uint8_t t = 0; t < TRACKS; t++)
    if (song.tracks[t].kind == k) return t;
  return fallback;
}
uint8_t liveSynthTrack() {
  return song.tracks[sel].kind == TrackKind::Synth ? sel : firstOfKind(TrackKind::Synth, sel);
}
uint8_t liveDrumTrack() {
  return song.tracks[sel].kind == TrackKind::Drum ? sel : firstOfKind(TrackKind::Drum, sel);
}

// ── Playback ─────────────────────────────────────────────────────────────────
static void noteOffActive(Active& a) {
  midi::noteOff(a.channel, a.pitch);
  if (!a.drum) monitor::noteOff(a.pitch);
  a.used = false;
}

static void startNote(const Track& tr, const NoteEvent& e, uint32_t tick) {
  bool drum = tr.kind == TrackKind::Drum;
  // Retrigger: stop the same pitch on the same channel if it's still sounding.
  Active* slot = nullptr;
  for (auto& a : active) {
    if (a.used && a.channel == tr.channel && a.pitch == e.pitch) noteOffActive(a);
    if (!a.used && !slot) slot = &a;
  }
  if (!slot) {  // all busy: cut the one ending soonest
    slot = &active[0];
    for (auto& a : active)
      if (a.offTick < slot->offTick) slot = &a;
    noteOffActive(*slot);
  }
  midi::noteOn(tr.channel, e.pitch, e.vel);
  if (drum) {
    int pad = player::drumPadForNote(e.pitch);
    monitor::drum(pad >= 0 ? (uint8_t)pad : 0);
  } else {
    monitor::noteOn(e.pitch);
  }
  *slot = {true, tr.channel, e.pitch, drum, tick + (e.len ? e.len : 1)};
}

static void processTick(uint32_t tick) {
  for (auto& a : active)
    if (a.used && (int32_t)(tick - a.offTick) >= 0) noteOffActive(a);

  for (uint8_t t = 0; t < TRACKS; t++) {
    Track& tr = song.tracks[t];
    Pattern& p = tr.cur();
    const uint32_t P = (uint32_t)p.length * TICKS_PER_STEP;
    const uint32_t rel = tick % P;
    for (auto& e : p.notes) {
      uint32_t fire = (uint32_t)(((int32_t)e.step * TICKS_PER_STEP + e.offset + (int32_t)P) % (int32_t)P);
      if (fire != rel) continue;
      if (e.flags & NF_SKIP_ONCE) {
        e.flags &= ~NF_SKIP_ONCE;
        continue;
      }
      if (!tr.mute) startNote(tr, e, tick);
    }
  }

  // Metronome on each beat (accent on the bar).
  const uint32_t beat = TICKS_PER_STEP * 4;
  if (tick % beat == 0 && (settings.metronome == 2 || (settings.metronome == 1 && recording())))
    monitor::click(tick % (beat * 4) == 0);
}

static void clockTask(void*) {
  for (;;) {
    if (isPlaying) {
      lock();
      uint32_t now = micros();
      if ((int32_t)(now - nextTickUs) > 100000) nextTickUs = now;  // fell far behind: resync
      while (isPlaying && (int32_t)(now - nextTickUs) >= 0) {
        processTick(nextTick);
        lastTickUs = nextTickUs;
        nextTick++;
        nextTickUs += tickUs();
      }
      midi::update();  // send this tick's BLE packet right away
      unlock();
    }
    vTaskDelay(1);
  }
}

void engineBegin() {
  mtx = xSemaphoreCreateRecursiveMutex();
  memset(active, 0, sizeof(active));
  memset(recHeld, 0, sizeof(recHeld));
  xTaskCreatePinnedToCore(clockTask, "seqclock", 4096, nullptr, 3, nullptr, 1);
}

// ── Transport ────────────────────────────────────────────────────────────────
void play() {
  Lock l;
  if (isPlaying) return;
  nextTick = 0;
  nextTickUs = micros();
  lastTickUs = nextTickUs;
  isPlaying = true;
}

void stop() {
  Lock l;
  if (!isPlaying) return;
  isPlaying = false;
  for (auto& a : active)
    if (a.used) noteOffActive(a);
  for (auto& r : recHeld) r.used = false;
  midi::update();
}

bool playing() { return isPlaying; }
void setRecordArmed(bool on) { recArmed = on; }
bool recordArmed() { return recArmed; }
bool recording() { return recArmed && isPlaying; }

int trackStep(uint8_t t) {
  if (!isPlaying || nextTick == 0 || t >= TRACKS) return -1;
  const Pattern& p = song.tracks[t].patterns[song.tracks[t].pattern];
  uint32_t P = (uint32_t)p.length * TICKS_PER_STEP;
  return (int)(((nextTick - 1) % P) / TICKS_PER_STEP);
}

// ── Live recording ───────────────────────────────────────────────────────────
// Song position in ticks right now, including the fraction since the last tick.
static float nowTick() {
  if (nextTick == 0) return 0;
  float frac = (float)(micros() - lastTickUs) / (float)tickUs();
  if (frac > 1.0f) frac = 1.0f;
  return (float)(nextTick - 1) + frac;
}

void recordPress(uint8_t key, uint8_t track, const uint8_t* pitches, uint8_t count, uint8_t vel) {
  Lock l;
  if (!recording() || key >= layout::KEY_COUNT || track >= TRACKS || !count) return;
  Track& tr = song.tracks[track];
  Pattern& p = tr.cur();
  const int P = (int)p.length * TICKS_PER_STEP;
  const float pos = nowTick();
  const int posTick = (int)lroundf(pos);
  const int rel = posTick % P;

  int step = (int)lroundf((float)rel / TICKS_PER_STEP);
  int offset = rel - step * TICKS_PER_STEP;
  if (step >= p.length) step -= p.length;  // rounded up past the end: belongs to step 0
  if (settings.quantize) offset = 0;
  if (offset < -TICKS_PER_STEP / 2) offset = -TICKS_PER_STEP / 2;
  if (offset > TICKS_PER_STEP / 2 - 1) offset = TICKS_PER_STEP / 2 - 1;

  // Has the engine already passed this note's trigger point? If not, it would
  // fire it again on top of what you're playing, so skip it once.
  int fire = (step * TICKS_PER_STEP + offset + P) % P;
  int delta = fire - rel;
  if (delta > P / 2) delta -= P;
  if (delta < -P / 2) delta += P;
  bool skip = (posTick + delta) > (int)(nextTick - 1);

  RecHeld& h = recHeld[key];
  h = {true, track, tr.pattern, (uint8_t)step, 0, {}, pos};
  for (uint8_t i = 0; i < count && i < 5; i++) {
    NoteEvent e{(uint8_t)step, (int8_t)offset, pitches[i], vel, TICKS_PER_STEP,
                (uint8_t)(skip ? NF_SKIP_ONCE : 0)};
    // Overdub replaces the same pitch on the same step.
    for (auto it = p.notes.begin(); it != p.notes.end(); ++it)
      if (it->step == e.step && it->pitch == e.pitch) {
        p.notes.erase(it);
        break;
      }
    if (addNote(p, e)) h.pitches[h.count++] = e.pitch;
  }
  if (!h.count) h.used = false;
}

void recordRelease(uint8_t key) {
  Lock l;
  if (key >= layout::KEY_COUNT || !recHeld[key].used) return;
  RecHeld& h = recHeld[key];
  h.used = false;
  Pattern& p = song.tracks[h.track].patterns[h.pattern];
  float held = nowTick() - h.startTick;
  if (held < 1) held = 1;
  uint16_t len = (uint16_t)lroundf(held);
  if (settings.quantize) {
    uint16_t steps = (uint16_t)lroundf(held / TICKS_PER_STEP);
    len = (steps < 1 ? 1 : steps) * TICKS_PER_STEP;
  }
  uint16_t maxLen = (uint16_t)p.length * TICKS_PER_STEP;
  if (len > maxLen) len = maxLen;
  for (auto& e : p.notes)
    if (e.step == h.step)
      for (uint8_t i = 0; i < h.count; i++)
        if (e.pitch == h.pitches[i]) e.len = len;
}

}  // namespace seq
