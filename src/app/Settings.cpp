#include "Settings.h"

#include <Preferences.h>
#include <string.h>

Settings settings;

// Fields are only ever added at the end, so a save from an older version is
// simply a shorter copy of the same layout: it loads as-is and the newer
// fields keep their defaults.
static const uint32_t SETTINGS_VERSION = 3;
static Settings saved;  // copy of what is in flash, to skip redundant writes

void settingsLoad() {
  Preferences prefs;
  if (!prefs.begin("cmc", true)) return;  // first boot: namespace doesn't exist yet
  uint32_t ver = prefs.getUInt("ver", 0);
  size_t len = prefs.getBytesLength("s");
  if (ver >= 1 && ver <= SETTINGS_VERSION && len > 0 && len <= sizeof(Settings)) {
    prefs.getBytes("s", &settings, len);
  }
  prefs.end();
  saved = settings;
}

void settingsSave() {
  if (memcmp(&saved, &settings, sizeof(Settings)) == 0) return;
  Preferences prefs;
  if (!prefs.begin("cmc", false)) return;
  prefs.putUInt("ver", SETTINGS_VERSION);
  prefs.putBytes("s", &settings, sizeof(Settings));
  prefs.end();
  saved = settings;
}
