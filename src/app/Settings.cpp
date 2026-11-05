#include "Settings.h"

#include <Preferences.h>
#include <string.h>

Settings settings;

static const uint32_t SETTINGS_VERSION = 2;  // bump when Settings changes
static Settings saved;  // copy of what is in flash, to skip redundant writes

void settingsLoad() {
  Preferences prefs;
  if (!prefs.begin("cmc", true)) return;  // first boot: namespace doesn't exist yet
  // Fields are only ever appended, so a shorter blob from an older build is a
  // valid prefix: load what's there and keep defaults for the rest.
  size_t len = prefs.getBytesLength("s");
  if (prefs.getUInt("ver", 0) == SETTINGS_VERSION && len > 0 && len <= sizeof(Settings)) {
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
