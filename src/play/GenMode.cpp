#include "GenMode.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_random.h>

#include <atomic>
#include <new>

#include "../app/Settings.h"
#include "../net/TrkrConvert.h"
#include "../seq/Engine.h"
#include "../seq/Song.h"
#include "LivePlayer.h"

namespace genmode {

static const gen::Genre& G(int i) { return *gen::GENRE_LIST[i]; }
int genreCount() { return gen::GENRE_COUNT; }
const char* genreName(int idx) { return G(((idx % genreCount()) + genreCount()) % genreCount()).name; }
static void invalidateNext();
static State st = State::Idle;
int genre() { return settings.genGenre % genreCount(); }
void setGenre(int idx) {
  settings.genGenre = (uint8_t)(((idx % genreCount()) + genreCount()) % genreCount());
  if (st != State::Idle) invalidateNext();  // the song already built was for the old genre
}
int freeHeapKb() { return (int)(esp_get_free_heap_size() / 1024); }

// Rough floor for the generator's working memory plus the song it builds.
static constexpr uint32_t MIN_FREE_BYTES = 66000;  // generator ~37 KB + the song it builds (up to ~27 KB)

static std::atomic<bool> abortReq{false}, producerAlive{false}, nextReady{false}, wantNext{false};
static seq::Song* nextSong = nullptr;  // built by the task, swapped in by applyNext()
static gen::Meta nextMeta;
static int nextGenre = 0;
static gen::Meta curMeta;
static int curGenre = 0;
static bool lowMem = false, skipPending = false, haveGen = false;
static uint8_t lastDefaultChan[seq::TRACKS] = {0};

State state() { return st; }
bool running() { return st != State::Idle; }
bool lowMemory() { return lowMem; }
const gen::Meta& meta() { return curMeta; }
int playingGenre() { return curGenre; }

// ── the song builder ─────────────────────────────────────────────────────────
// Receives the song as the generator makes it and builds the device song
// directly: the whole song never exists in the website's format.
class DeviceSink : public gen::Sink {
 public:
  explicit DeviceSink(seq::Song& out) : imp(out) {}
  trkr::Importer imp;
  trkr::Meta tmeta;
  gen::Meta meta;
  bool ok = true;
  const char* err = nullptr;
  bool begin(const trkr::Song& head, const gen::Meta& m) override {
    meta = m;
    ok = imp.begin(head, tmeta, &err);
    return ok && !abortReq;
  }
  bool pattern(trkr::Pattern&& p) override {
    ok = imp.addPattern(p, &err);
    return ok && !abortReq;
  }
};

static void producer(void*) {
  while (!abortReq) {
    if (nextReady || !wantNext) {
      vTaskDelay(pdMS_TO_TICKS(40));
      continue;
    }
    if (esp_get_free_heap_size() < MIN_FREE_BYTES) {
      lowMem = true;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    seq::Song* s = new (std::nothrow) seq::Song();
    if (!s) {
      lowMem = true;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    lowMem = false;
    const int g = genre();
    const uint32_t seed = esp_random() % 1000000000UL;
    DeviceSink sink(*s);
    const bool done = gen::generateStream(G(g), seed, sink);
    if (done && sink.ok && !abortReq) {
      sink.imp.finish();
      nextMeta = std::move(sink.meta);
      nextGenre = g;
      nextSong = s;
      nextReady = true;
    } else {
      delete s;
      vTaskDelay(pdMS_TO_TICKS(100));  // a song that doesn't fit: try another
    }
  }
  producerAlive = false;
  vTaskDelete(nullptr);
}

// ── swapping songs ───────────────────────────────────────────────────────────
// Call with the engine lock held. Mutes, and channels the user changed, carry
// over from the song that was playing.
static void applyNext() {
  if (!nextReady || !nextSong) return;
  seq::Song& cur = seq::song;
  uint8_t defaults[seq::TRACKS];
  for (uint8_t t = 0; t < seq::TRACKS; t++) {
    defaults[t] = nextSong->tracks[t].channel;
    if (haveGen) {
      nextSong->tracks[t].mute = cur.tracks[t].mute;
      if (cur.tracks[t].channel != lastDefaultChan[t]) nextSong->tracks[t].channel = cur.tracks[t].channel;
    }
  }
  memcpy(lastDefaultChan, defaults, sizeof(defaults));
  cur = std::move(*nextSong);
  delete nextSong;
  nextSong = nullptr;
  curMeta = std::move(nextMeta);
  curGenre = nextGenre;
  nextReady = false;
  haveGen = true;
  wantNext = true;
  seq::songForgetName();  // a generated song isn't any saved file: saving it must ask for a name
}

static void wrapHook() { applyNext(); }  // the arrangement is starting over: new song, exactly on the beat

static void invalidateNext() {
  seq::Lock l;
  if (nextReady && nextSong) {
    delete nextSong;
    nextSong = nullptr;
    nextReady = false;
  }
  wantNext = true;
}

// ── play-along ───────────────────────────────────────────────────────────────
const gen::ChordSpan* chordNow() {
  const int32_t step = seq::songStep();
  return step < 0 ? nullptr : gen::chordAt(curMeta, (uint32_t)step);
}
gen::Along along() { return (gen::Along)(settings.genAlong > 3 ? 1 : settings.genAlong); }
void setAlong(gen::Along a) { settings.genAlong = (uint8_t)a; }
const char* alongName(gen::Along a) {
  switch (a) {
    case gen::Along::Free: return "any note";
    case gen::Along::Chord: return "chord tones only";
    case gen::Along::Snap: return "snap to chord";
    default: return "scale only";
  }
}
static int alongFilter(int note) {
  const gen::ChordSpan* c = chordNow();
  if (!c) return note;
  return gen::alongNote(along(), c->pcs, c->scale, note);
}

// ── transport ────────────────────────────────────────────────────────────────
void begin() {}

void start() {
  if (st != State::Idle) return;
  lowMem = false;
  abortReq = false;
  nextReady = false;
  wantNext = true;
  haveGen = false;
  skipPending = false;
  producerAlive = true;
  if (xTaskCreatePinnedToCore(producer, "gen", 10240, nullptr, 1, nullptr, 0) != pdPASS) {
    producerAlive = false;
    lowMem = true;
    return;
  }
  seq::setWrapHook(wrapHook);
  player::setNoteFilter(alongFilter);
  st = State::Generating;
}

void stop() {
  if (st == State::Idle) return;
  seq::setWrapHook(nullptr);
  player::setNoteFilter(nullptr);
  seq::stop();
  abortReq = true;
  for (int i = 0; i < 400 && producerAlive; i++) vTaskDelay(pdMS_TO_TICKS(5));
  seq::Lock l;
  if (nextSong) {
    delete nextSong;
    nextSong = nullptr;
  }
  nextReady = false;
  haveGen = false;
  skipPending = false;
  st = State::Idle;
}

void next() {
  if (st == State::Idle) {
    start();
    return;
  }
  skipPending = true;  // tick() swaps as soon as the next song is ready
}

static void playNow() {
  seq::Lock l;
  seq::stop();
  applyNext();
  seq::setSongStart(0);
  seq::play();
}

void tick() {
  if (st == State::Idle) return;
  if (st == State::Generating && nextReady) {
    playNow();
    st = State::Playing;
  } else if (skipPending && nextReady) {
    skipPending = false;
    playNow();
  }
}

}  // namespace genmode
