#include "Settings.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <string.h>

Settings settings;

// Settings live in a file on the LittleFS partition, next to the songs.
// They used to be in NVS, but the start of flash (where NVS sits) is
// overwritten every time the merged firmware image is flashed at 0x0, which
// reset everything (including which song to reopen) after each update.
// NVS is still read once as a fallback so nothing is lost on the way over.
static const char* PATH = "/settings.bin";
static const char* TMP = "/settings.tmp";
static const uint32_t MAGIC = 0x54534D43;    // "CMST"
static const uint32_t SETTINGS_VERSION = 2;  // bump only if fields change meaning
static Settings saved;  // copy of what is stored, to skip redundant writes

// Fields are only ever appended, so a shorter blob from an older build is a
// valid prefix: load what's there and keep defaults for the rest.
static bool loadFromFile() {
  if (!LittleFS.begin(true)) return false;
  File f = LittleFS.open(PATH, "r");
  if (!f) return false;
  uint32_t magic = 0, ver = 0;
  uint16_t len = 0;
  bool ok = f.read((uint8_t*)&magic, 4) == 4 && magic == MAGIC && f.read((uint8_t*)&ver, 4) == 4 &&
            ver == SETTINGS_VERSION && f.read((uint8_t*)&len, 2) == 2 && len > 0 &&
            len <= sizeof(Settings);
  if (ok) {
    Settings tmp = settings;  // defaults for any fields the file doesn't have
    ok = f.read((uint8_t*)&tmp, len) == len;
    if (ok) settings = tmp;
  }
  f.close();
  return ok;
}

static void loadFromNvs() {
  Preferences prefs;
  if (!prefs.begin("cmc", true)) return;  // namespace doesn't exist
  size_t len = prefs.getBytesLength("s");
  if (prefs.getUInt("ver", 0) == SETTINGS_VERSION && len > 0 && len <= sizeof(Settings))
    prefs.getBytes("s", &settings, len);
  prefs.end();
}

void settingsLoad() {
  if (!loadFromFile()) {
    loadFromNvs();
    memset(&saved, 0xFF, sizeof(saved));  // force the first save to write the file
    settingsSave();
    return;
  }
  saved = settings;
}

void settingsSave() {
  if (memcmp(&saved, &settings, sizeof(Settings)) == 0) return;
  if (!LittleFS.begin(true)) return;
  File f = LittleFS.open(TMP, "w");
  if (!f) return;
  const uint16_t len = sizeof(Settings);
  f.write((const uint8_t*)&MAGIC, 4);
  f.write((const uint8_t*)&SETTINGS_VERSION, 4);
  f.write((const uint8_t*)&len, 2);
  f.write((const uint8_t*)&settings, len);
  const bool ok = f.getWriteError() == 0;
  f.close();
  if (!ok) {
    LittleFS.remove(TMP);
    return;
  }
  // Swap in the new file only once it's fully written.
  LittleFS.remove(PATH);
  if (LittleFS.rename(TMP, PATH)) saved = settings;
}
