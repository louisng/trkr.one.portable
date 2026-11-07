#pragma once
// Songs saved on the Cardputer's flash (LittleFS), plus what the cloud sync
// needs to know about each one. Files live in /songs/<file>.trk; the index
// (/songs/index.txt) keeps one line per song:
//   file <TAB> cloudId <TAB> cloudVersion <TAB> dirty <TAB> name
// All functions are safe to call from the sync task and the UI.
#include <Arduino.h>
#include <stdint.h>

#include <vector>

#include "Song.h"

namespace store {

struct Entry {
  char file[12] = "";      // 8 hex chars, no extension
  char cloudId[40] = "";   // empty = only on this device
  uint32_t version = 0;    // cloud version this copy is based on
  bool dirty = false;      // changed on the device since the last sync
  char name[48] = "";
};

bool begin();
bool ready();
std::vector<Entry> list();
bool find(const char* cloudId, Entry& out);

// Load a song by file name.
bool load(const char* file, song::Song& out, const char** error = nullptr);
// Save (create or replace) a song; fills e.file for new entries. Marks the
// entry dirty unless `fromCloud`.
bool save(Entry& e, const song::Song& s, bool fromCloud);
// Write a downloaded file straight from a stream, then validate it.
bool saveStream(Entry& e, Stream& in, bool fromCloud, const char** error = nullptr);
bool update(const Entry& e);  // index fields only
bool remove(const char* file);

// Path of a song file, for streaming uploads.
String path(const char* file);

}  // namespace store
