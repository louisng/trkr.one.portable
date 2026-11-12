#include "GenMode.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>

#include "../app/Settings.h"
#include "../gen/Along.h"
#include "../gen/GenreData.h"
#include "Sequencer.h"

namespace genmode {

static State st = State::Idle;
static bool lowMem = false;
static gen::Meta curMeta;        // map of the song that is playing (main thread only)
static int curGenre = 0;
static bool lockedOn = false;
static char chordBuf[14] = "";

static const gen::Genre& G(int i) { return *gen::GENRE_LIST[i]; }
int genreCount() { return gen::GENRE_COUNT; }
const char* genreName(int idx) { return G(((idx % genreCount()) + genreCount()) % genreCount()).name; }
int genre() { return settings.genGenre; }
int playingGenre() { return curGenre; }
State state() { return st; }
bool running() { return st != State::Idle; }
bool locked() { return lockedOn; }
bool lowMemory() { return lowMem; }
const gen::Meta& meta() { return curMeta; }
uint16_t currentBpm() { return seq::loaded() ? seq::current().bpm : 0; }
uint32_t currentSeed() { return curMeta.seed; }
int freeHeapKb() { return (int)(esp_get_free_heap_size() / 1024); }

// ── the queue between the generator task and the sequencer ───────────────────
// The first pattern of every song also carries that song's header (tempo,
// tracks, chord map), so the switch happens exactly on a pattern boundary.
struct QItem {
  song::Pattern pat;
  bool newSong = false;
  song::Song head;
  gen::Meta meta;
  int genre = 0;
};
static constexpr int CAP = 3;
static QItem ring[CAP];
static int qHead = 0, qCount = 0;
static SemaphoreHandle_t qMutex = nullptr;
static std::atomic<bool> abortReq{false};
static std::atomic<bool> producerAlive{false};

static void qClear() {
  xSemaphoreTake(qMutex, portMAX_DELAY);
  for (auto& it : ring) it = QItem();
  qHead = qCount = 0;
  xSemaphoreGive(qMutex);
}

// Producer side (generator task): waits while the queue is full.
static bool qPush(QItem&& it) {
  for (;;) {
    if (abortReq) return false;
    xSemaphoreTake(qMutex, portMAX_DELAY);
    if (qCount < CAP) {
      ring[(qHead + qCount) % CAP] = std::move(it);
      qCount++;
      xSemaphoreGive(qMutex);
      return true;
    }
    xSemaphoreGive(qMutex);
    vTaskDelay(8 / portTICK_PERIOD_MS);
  }
}

static void applyMutes(song::Song& s) {
  for (uint8_t i = 0; i < s.trackCount && i < TRACKS; i++) {
    s.tracks[i].mute = (settings.genMuted >> i) & 1;
    s.tracks[i].ch = settings.genCh[i];   // the parts' MIDI channels are the user's, not the song's
  }
}

// Consumer side: the sequencer pulls patterns from here.
struct Source : seq::PatternSource {
  bool ready() override {
    xSemaphoreTake(qMutex, portMAX_DELAY);
    bool r = qCount > 0;
    xSemaphoreGive(qMutex);
    return r;
  }
  void take(song::Pattern& out, bool& newSong) override {
    xSemaphoreTake(qMutex, portMAX_DELAY);
    QItem it = std::move(ring[qHead]);
    ring[qHead] = QItem();
    qHead = (qHead + 1) % CAP;
    qCount--;
    xSemaphoreGive(qMutex);
    out = std::move(it.pat);
    newSong = it.newSong;
    if (it.newSong) {
      song::Song& s = seq::current();
      s = std::move(it.head);       // tempo + tracks of the song that now begins
      applyMutes(s);
      curMeta = std::move(it.meta);
      curGenre = it.genre;
      st = State::Playing;
    }
  }
  bool finished() override { return false; }
};
static Source source;

// ── the generator task ───────────────────────────────────────────────────────
struct Feed : gen::Sink {
  int genre = 0;
  bool first = true;
  song::Song headCopy;
  gen::Meta metaCopy;
  bool begin(const song::Song& head, const gen::Meta& m) override {
    headCopy = head;
    metaCopy = m;
    first = true;
    return !abortReq;
  }
  bool pattern(song::Pattern&& p) override {
    QItem it;
    it.pat = std::move(p);
    if (first) {
      it.newSong = true;
      it.head = std::move(headCopy);
      it.meta = std::move(metaCopy);
      it.genre = genre;
      first = false;
    }
    return qPush(std::move(it));
  }
};

static uint32_t lastSeed = 0;
static int lastGenre = 0;
static bool haveLast = false;

static void producer(void*) {
  while (!abortReq) {
    int g = settings.genGenre;
    uint32_t seed = esp_random() % 1000000000UL;
    if (lockedOn && haveLast) {  // keep repeating the last song
      g = lastGenre;
      seed = lastSeed;
    }
    lastGenre = g;
    lastSeed = seed;
    haveLast = true;
    Feed feed;
    feed.genre = g;
    gen::generateStream(G(g), seed, feed);
  }
  producerAlive = false;
  vTaskDelete(nullptr);
}

// Asks the generator task to stop and waits until it has. (It checks between
// patterns, so this takes a few milliseconds.)
static bool stopProducer() {
  abortReq = true;
  for (int i = 0; i < 600 && producerAlive; i++) vTaskDelay(5 / portTICK_PERIOD_MS);
  if (producerAlive) return false;  // still busy: leave the stop request standing
  abortReq = false;
  return true;
}

// Rough floor for the generator's working memory plus the queue.
static constexpr uint32_t MIN_FREE_BYTES = 70000;

static bool startPlayback() {
  if (producerAlive) return false;  // a previous generator task hasn't finished stopping
  // Let go of the old song first: the patterns waiting in the queue and the
  // old song's header are what hold most of the memory while it plays.
  qClear();
  seq::current() = song::Song();
  // The previous song's generator task and patterns are freed a moment after
  // they stop (the idle task releases a deleted task's stack), so give the
  // heap a short while to settle before deciding there is too little.
  for (int i = 0; i < 20 && esp_get_free_heap_size() < MIN_FREE_BYTES; i++) vTaskDelay(25 / portTICK_PERIOD_MS);
  if (esp_get_free_heap_size() < MIN_FREE_BYTES) {
    lowMem = true;
    return false;
  }
  lowMem = false;
  qClear();
  curMeta = gen::Meta();
  haveLast = false;
  abortReq = false;
  producerAlive = true;
  xTaskCreatePinnedToCore(producer, "gen", 10240, nullptr, 1, nullptr, 0);
  seq::setLoop(true);
  seq::playStream(&source);
  st = State::Generating;
  return true;
}

// ── transport ────────────────────────────────────────────────────────────────
void begin() {
  if (!qMutex) qMutex = xSemaphoreCreateMutex();
  if (settings.genGenre >= genreCount()) settings.genGenre = 0;
  if (settings.genAlong > 3) settings.genAlong = 1;
}

void start() {
  if (st != State::Idle) return;
  startPlayback();
}

void stop() {
  if (st == State::Idle) return;
  seq::stop();
  if (stopProducer()) qClear();
  st = State::Idle;
}

void toggle() {
  if (running()) stop();
  else start();
}

void next() {
  if (st == State::Idle) { start(); return; }
  seq::stop();
  st = State::Idle;
  if (stopProducer()) startPlayback();
}

void setGenre(int idx) {
  int n = genreCount();
  settings.genGenre = (uint8_t)(((idx % n) + n) % n);  // used when the next song is built
}

void setLocked(bool on) { lockedOn = on; }

void nudgeTempo(int delta) {
  if (!seq::loaded() || st != State::Playing) return;
  int bpm = (int)seq::current().bpm + delta;
  seq::current().bpm = (uint16_t)constrain(bpm, 40, 220);
  seq::changed();
}

void tick() {
  // Nothing to do on the main thread: the sequencer pulls patterns itself.
}

// ── channels ─────────────────────────────────────────────────────────────────
static const uint8_t DEFAULT_CH[TRACKS] = {10, 10, 10, 10, 3, 2, 1, 4};
uint8_t defaultChannel(uint8_t t) { return t < TRACKS ? DEFAULT_CH[t] : 1; }
uint8_t channelOf(uint8_t t) { return t < TRACKS ? settings.genCh[t] : 1; }

void setChannel(uint8_t t, uint8_t ch) {
  if (t >= TRACKS) return;
  ch = (uint8_t)constrain((int)ch, 1, 16);
  if (settings.genCh[t] == ch) return;
  if (seq::loaded() && t < seq::current().trackCount) seq::silenceTrack(t);  // let go of held notes on the old channel
  settings.genCh[t] = ch;
  if (seq::loaded() && t < seq::current().trackCount) seq::current().tracks[t].ch = ch;
}
void resetChannel(uint8_t t) { setChannel(t, defaultChannel(t)); }

// ── mutes ────────────────────────────────────────────────────────────────────
bool trackMuted(uint8_t t) { return t < TRACKS && ((settings.genMuted >> t) & 1); }

void toggleMute(uint8_t t) {
  if (t >= TRACKS) return;
  settings.genMuted ^= (uint16_t)(1u << t);
  if (seq::loaded() && t < seq::current().trackCount) {
    bool m = trackMuted(t);
    seq::current().tracks[t].mute = m;
    if (m) seq::silenceTrack(t);
  }
}

void unmuteAll() {
  settings.genMuted = 0;
  if (seq::loaded())
    for (uint8_t i = 0; i < seq::current().trackCount; i++) seq::current().tracks[i].mute = false;
}

uint8_t trackCount() { return st == State::Playing && seq::loaded() ? seq::current().trackCount : 0; }
const char* trackName(uint8_t t) { return t < trackCount() ? seq::current().tracks[t].name : ""; }
uint8_t trackChannel(uint8_t t) { return t < trackCount() ? seq::current().tracks[t].ch : 0; }

// ── what is playing ──────────────────────────────────────────────────────────
const char* chordName() {
  const gen::ChordSpan* c = gen::chordAt(curMeta, seq::songPosition());
  snprintf(chordBuf, sizeof(chordBuf), "%s", c ? c->sym : "");
  return chordBuf;
}
const char* sectionName() {
  const gen::SectionSpan* s = gen::sectionAt(curMeta, seq::songPosition() / 16);
  return s ? s->name : "";
}

// ── play-along keyboard ──────────────────────────────────────────────────────
AlongMode along() { return (AlongMode)settings.genAlong; }
void setAlong(AlongMode m) { settings.genAlong = (uint8_t)m; }
void cycleAlong() { settings.genAlong = (settings.genAlong + 1) % 4; }
const char* alongName(AlongMode m) {
  switch (m) {
    case AlongMode::Free: return "FREE";
    case AlongMode::Chord: return "CHORD";
    case AlongMode::Snap: return "SNAP";
    case AlongMode::Scale: return "SCALE";
  }
  return "";
}

// Pitch the key should sound, or -1 for silence.
int filterNote(int note) {
  if (along() == AlongMode::Free || st != State::Playing) return note;
  const gen::ChordSpan* c = gen::chordAt(curMeta, seq::songPosition());
  if (!c) return note;
  return gen::alongNote((gen::Along)along(), c->pcs, c->scale, note);
}
bool noteBlocked(int note) { return filterNote(note) < 0; }

}  // namespace genmode
