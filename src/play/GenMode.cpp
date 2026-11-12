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
static volatile State st = State::Idle;
int genre() { return settings.genGenre % genreCount(); }
void setGenre(int idx) {
  settings.genGenre = (uint8_t)(((idx % genreCount()) + genreCount()) % genreCount());
  if (st != State::Idle) invalidateNext();  // the song already built was for the old genre
}
int freeHeapKb() { return (int)(esp_get_free_heap_size() / 1024); }
int largestBlockKb() { return (int)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024); }

// Memory policy. The generator needs ~37 KB while it works and a song holds up
// to ~27 KB of notes, so:
//  - the next song is built while one plays only if there is comfortable room;
//  - otherwise the playing song is freed first and the next is built in the
//    space it leaves (a short gap between songs instead of a stall);
//  - a build that runs low on memory stops cleanly instead of crashing.
// Peak memory (KB) of building a song, per genre, measured on a PC over many
// seeds (test/host/genre_mem_test.cpp): the generator's own working memory plus the song.
struct GenreCost { const char* id; uint8_t kb; };
static const GenreCost GENRE_COST[] = {
    {"disco", 61},       {"emo", 49},     {"city_pop", 48}, {"synth_pop", 44}, {"neo_soul", 42}, {"funk", 42},
    {"electronica", 42}, {"pop", 40},     {"dnb", 36},      {"synthwave", 34}, {"bossa_nova", 33}, {"trap", 29},
    {"house", 29},       {"reggae", 23},  {"lofi", 22},     {"boom_bap", 21},  {"ambient", 13},
};
static constexpr uint32_t GUARD_FREE = 20000;     // a build stops if free memory falls below this
static constexpr uint32_t GUARD_BLOCK = 6000;     // ... or the largest free block below this
static bool heapLow() {
  return esp_get_free_heap_size() < GUARD_FREE || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < GUARD_BLOCK;
}

// What a genre needs free before building starts: its measured peak with a little
// extra for allocator overhead, plus the margin the build keeps in hand.
static uint32_t genreNeedBytes(int g) {
  uint32_t kb = 64;  // unknown genre: assume heavy
  for (const auto& c : GENRE_COST)
    if (strcmp(c.id, G(g).id) == 0) kb = c.kb;
  return kb * 1024 * 112 / 100 + GUARD_FREE;
}
int needKb() { return (int)((genreNeedBytes(genre()) + 1023) / 1024); }

static std::atomic<bool> abortReq{false}, producerAlive{false}, nextReady{false}, wantNext{false};
static std::atomic<bool> rebuilding_{false};       // the playing song was freed; the next is being built (a gap)
static std::atomic<bool> prefetchBlocked_{false};  // no room to build the next song while this one plays
static seq::Song* nextSong = nullptr;  // built by the task, swapped in by applyNext()
static gen::Meta nextMeta;
static int nextGenre = 0;
static gen::Meta curMeta;
static int curGenre = 0;
static bool lowMem = false, skipPending = false, haveGen = false;
static uint8_t lastDefaultChan[seq::TRACKS] = {0};
// The user's mutes and channel choices, taken from the song that was playing.
static bool carryHave = false;
static bool carryMute[seq::TRACKS];
static uint8_t carryChan[seq::TRACKS];

State state() { return st; }
bool rebuilding() { return rebuilding_; }
bool prefetchBlocked() { return prefetchBlocked_; }
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
  bool ranLow = false;  // stopped because memory got too low
  const char* err = nullptr;
  bool begin(const trkr::Song& head, const gen::Meta& m) override {
    meta = m;
    ok = imp.begin(head, tmeta, &err);
    return ok && !abortReq && !check();
  }
  bool pattern(trkr::Pattern&& p) override {
    ok = imp.addPattern(p, &err);
    return ok && !abortReq && !check();
  }

 private:
  bool check() {  // true = stop: a failed allocation is fatal on this device, so never get that close
    if (heapLow()) {
      ranLow = true;
      return true;
    }
    return false;
  }
};

static void producer(void*) {
  int lowTries = 0;
  while (!abortReq) {
    if (nextReady || !wantNext) {
      vTaskDelay(pdMS_TO_TICKS(40));
      continue;
    }
    // A generated song is in memory and playing: the next one must fit beside it.
    const bool resident = st == State::Playing && !rebuilding_;
    if (esp_get_free_heap_size() < genreNeedBytes(genre())) {
      if (resident) prefetchBlocked_ = true;  // not an error: the next song is built when this one ends
      else lowMem = true;
      vTaskDelay(pdMS_TO_TICKS(300));
      continue;
    }
    prefetchBlocked_ = false;
    seq::Song* s = new (std::nothrow) seq::Song();
    if (!s) {
      if (resident) prefetchBlocked_ = true;
      else lowMem = true;
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
      lowTries = 0;
    } else {
      delete s;
      if (sink.ranLow && ++lowTries < 3) continue;  // another seed may be lighter: try again at once
      if (sink.ranLow) {  // ran short of memory three times running: wait a moment
        lowTries = 0;
        if (resident) prefetchBlocked_ = true;
        else lowMem = true;
        vTaskDelay(pdMS_TO_TICKS(800));
      } else {
        vTaskDelay(pdMS_TO_TICKS(100));  // a song that doesn't fit the device: try another
      }
    }
  }
  producerAlive = false;
  vTaskDelete(nullptr);
}

// ── swapping songs ───────────────────────────────────────────────────────────
// Remember the user's mutes and channel choices (engine lock held).
static void snapshotCarry() {
  for (uint8_t t = 0; t < seq::TRACKS; t++) {
    carryMute[t] = seq::song.tracks[t].mute;
    carryChan[t] = seq::song.tracks[t].channel;
  }
  carryHave = true;
}

// Call with the engine lock held. Mutes, and channels the user changed, carry
// over from the song that was playing.
static void applyNext() {
  if (!nextReady || !nextSong) return;
  if (haveGen && !carryHave) snapshotCarry();
  uint8_t defaults[seq::TRACKS];
  for (uint8_t t = 0; t < seq::TRACKS; t++) {
    defaults[t] = nextSong->tracks[t].channel;
    if (haveGen) {
      nextSong->tracks[t].mute = carryMute[t];
      if (carryChan[t] != lastDefaultChan[t]) nextSong->tracks[t].channel = carryChan[t];
    }
  }
  carryHave = false;
  memcpy(lastDefaultChan, defaults, sizeof(defaults));
  seq::song = std::move(*nextSong);
  delete nextSong;
  nextSong = nullptr;
  curMeta = std::move(nextMeta);
  curGenre = nextGenre;
  nextReady = false;
  haveGen = true;
  wantNext = true;
  seq::songForgetName();  // a generated song isn't any saved file: saving it must ask for a name
}

// songInitInto() empties the note lists but a cleared std::vector keeps its
// memory, so freeing a song needs this: swap each list with an empty one.
static void releaseSongMemory(seq::Song& s) {
  for (auto& tr : s.tracks)
    for (auto& p : tr.patterns) {
      std::vector<seq::NoteEvent>().swap(p.notes);
      std::vector<seq::StepFx>().swap(p.fx);
    }
}

// No room to build the next song beside this one: stop, free this one, and let
// the next be built in the space it leaves (engine lock held).
static void beginRebuild() {
  snapshotCarry();
  seq::stop();
  seq::songInitInto(seq::song);
  releaseSongMemory(seq::song);
  curMeta = gen::Meta();  // its chord list goes too
  seq::songForgetName();
  rebuilding_ = true;
  prefetchBlocked_ = false;
  wantNext = true;
}

// The arrangement is starting over: swap in the next song exactly on the beat;
// if there was no room to build it in advance, build it now (with a gap).
static void wrapHook() {
  if (nextReady) applyNext();
  else if (prefetchBlocked_ && !rebuilding_) beginRebuild();
}

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
  carryHave = false;
  rebuilding_ = false;
  prefetchBlocked_ = false;
  skipPending = false;
  {
    // Generated mode replaces the open song anyway: let go of its memory now,
    // so a big song doesn't leave too little room to build the first one.
    seq::Lock l;
    seq::stop();
    seq::songInitInto(seq::song);
    releaseSongMemory(seq::song);
    seq::songForgetName();
  }
  producerAlive = true;
  if (xTaskCreatePinnedToCore(producer, "gen", 12288, nullptr, 1, nullptr, 0) != pdPASS) {
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
  carryHave = false;
  rebuilding_ = false;
  prefetchBlocked_ = false;
  skipPending = false;
  st = State::Idle;
}

void next() {
  if (st == State::Idle) {
    start();
    return;
  }
  if (!nextReady && prefetchBlocked_ && !rebuilding_) {  // no room beside this song: free it, build the next
    seq::Lock l;
    beginRebuild();
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
  } else if (rebuilding_ && nextReady) {
    playNow();
    rebuilding_ = false;
  } else if (skipPending && nextReady) {
    skipPending = false;
    playNow();
  }
}

}  // namespace genmode
