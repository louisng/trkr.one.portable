#include "SongStore.h"

#include <LittleFS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

namespace store {

static bool ok = false;
static SemaphoreHandle_t mtx = nullptr;
static const char* INDEX = "/songs/index.txt";

struct Lock {
  Lock() { if (mtx) xSemaphoreTakeRecursive(mtx, portMAX_DELAY); }
  ~Lock() { if (mtx) xSemaphoreGiveRecursive(mtx); }
};

String path(const char* file) { return String("/songs/") + file + ".trk"; }

bool begin() {
  if (!mtx) mtx = xSemaphoreCreateRecursiveMutex();
  // Format on first use (true): the partition starts out blank.
  ok = LittleFS.begin(true);
  if (ok && !LittleFS.exists("/songs")) LittleFS.mkdir("/songs");
  return ok;
}
bool ready() { return ok; }

static void copyField(char* dst, size_t n, const char* src, size_t len) {
  size_t m = len < n - 1 ? len : n - 1;
  memcpy(dst, src, m);
  dst[m] = 0;
}

static std::vector<Entry> readIndex() {
  std::vector<Entry> out;
  File f = LittleFS.open(INDEX, "r");
  if (!f) return out;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;
    Entry e;
    int a = line.indexOf('\t'), b = line.indexOf('\t', a + 1), c = line.indexOf('\t', b + 1),
        d = line.indexOf('\t', c + 1);
    if (a < 0 || b < 0 || c < 0 || d < 0) continue;
    copyField(e.file, sizeof(e.file), line.c_str(), a);
    copyField(e.cloudId, sizeof(e.cloudId), line.c_str() + a + 1, b - a - 1);
    e.version = strtoul(line.c_str() + b + 1, nullptr, 10);
    e.dirty = line.charAt(c + 1) == '1';
    copyField(e.name, sizeof(e.name), line.c_str() + d + 1, line.length() - d - 1);
    out.push_back(e);
  }
  return out;
}

static bool writeIndex(const std::vector<Entry>& list) {
  File f = LittleFS.open("/songs/index.tmp", "w");
  if (!f) return false;
  for (const auto& e : list) f.printf("%s\t%s\t%lu\t%d\t%s\n", e.file, e.cloudId, (unsigned long)e.version, e.dirty ? 1 : 0, e.name);
  f.close();
  LittleFS.remove(INDEX);
  return LittleFS.rename("/songs/index.tmp", INDEX);
}

std::vector<Entry> list() {
  Lock l;
  return ok ? readIndex() : std::vector<Entry>();
}

bool find(const char* cloudId, Entry& out) {
  Lock l;
  for (const auto& e : readIndex())
    if (cloudId[0] && strcmp(e.cloudId, cloudId) == 0) {
      out = e;
      return true;
    }
  return false;
}

bool load(const char* file, song::Song& out, const char** error) {
  Lock l;
  File f = LittleFS.open(path(file), "r");
  if (!f) {
    if (error) *error = "file missing";
    return false;
  }
  return song::load(f, out, error);
}

static void newFileName(char* out) {
  auto existing = readIndex();
  for (;;) {
    snprintf(out, 12, "%08lx", (unsigned long)esp_random());
    bool clash = false;
    for (const auto& e : existing)
      if (strcmp(e.file, out) == 0) clash = true;
    if (!clash) return;
  }
}

static bool upsert(Entry& e) {
  auto all = readIndex();
  bool found = false;
  for (auto& x : all)
    if (strcmp(x.file, e.file) == 0) {
      x = e;
      found = true;
    }
  if (!found) all.insert(all.begin(), e);
  return writeIndex(all);
}

bool save(Entry& e, const song::Song& s, bool fromCloud) {
  Lock l;
  if (!ok) return false;
  if (!e.file[0]) newFileName(e.file);
  File f = LittleFS.open(path(e.file), "w");
  if (!f) return false;
  song::save(f, s);
  f.close();
  strncpy(e.name, s.name, sizeof(e.name) - 1);
  e.dirty = !fromCloud;
  return upsert(e);
}

bool saveStream(Entry& e, Stream& in, bool fromCloud, const char** error) {
  Lock l;
  if (!ok) return false;
  bool isNew = !e.file[0];
  if (isNew) newFileName(e.file);
  String tmp = String("/songs/") + e.file + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  uint8_t buf[512];
  for (;;) {
    int n = in.readBytes(buf, sizeof(buf));
    if (n <= 0) break;
    f.write(buf, n);
  }
  f.close();
  // Validate before replacing the old copy.
  song::Song* s = new song::Song();
  File r = LittleFS.open(tmp, "r");
  bool valid = r && song::load(r, *s, error);
  r.close();
  if (valid) strncpy(e.name, s->name, sizeof(e.name) - 1);
  delete s;
  if (!valid) {
    LittleFS.remove(tmp);
    return false;
  }
  LittleFS.remove(path(e.file));
  LittleFS.rename(tmp, path(e.file));
  e.dirty = !fromCloud;
  return upsert(e);
}

bool update(const Entry& e) {
  Lock l;
  Entry copy = e;
  return upsert(copy);
}

bool remove(const char* file) {
  Lock l;
  auto all = readIndex();
  std::vector<Entry> keep;
  for (const auto& e : all)
    if (strcmp(e.file, file) != 0) keep.push_back(e);
  LittleFS.remove(path(file));
  return writeIndex(keep);
}

}  // namespace store
