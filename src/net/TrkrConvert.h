#pragma once
// Converts between a song from trkr.one (numbered patterns shared by all
// tracks, plus an order list) and the firmware's own song (per-track patterns,
// scenes, arrangement).
//
//   website -> device: every distinct pattern becomes a scene; each track
//     keeps one slot per DIFFERENT bit of music it plays (so a drum loop used
//     in twenty patterns takes one slot, and the limit of 8 patterns per track
//     is rarely a problem). The order list becomes the arrangement.
//   device -> website: every scene used by the arrangement becomes a pattern;
//     shorter track patterns are repeated to fill it, as they loop on the
//     device.
//
// What the device doesn't keep (swing, key, instrument names, strum) is
// remembered in a Meta so it survives a trip through the device.
#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "../seq/Song.h"
#include "TrkrSong.h"

namespace trkr {

struct Meta {
  uint8_t swing = 0, key = 0, scale = 0, trackCount = 0;
  char title[48] = "";   // the song's title online (the device keeps a shorter name)
  struct TrackMeta {
    char name[16] = "";  // full track name (the device keeps 5 characters)
    char voice[12] = "";
    uint8_t type = 4;   // TrackType
    uint8_t vol = 80;   // 0-100
    uint8_t strum = 0;
  } tracks[MAX_TRACKS];
};

// Builds a device song one pattern at a time, as a generator makes them: each
// call is one pattern played once, in play order (a repeated pattern is fine:
// identical music is stored once). Nothing but the finished device song is
// ever held, so a whole generated song never exists in the website format.
class Importer {
 public:
  explicit Importer(seq::Song& out) : out_(out) {}
  // tempo, tracks (and, in meta, what the device doesn't keep); head's patterns are ignored
  bool begin(const Song& head, Meta& meta, const char** error);
  bool addPattern(const Pattern& p, const char** error);
  void finish();
  uint8_t scenes() const { return (uint8_t)scenes_; }

 private:
  seq::Song& out_;
  uint8_t nT_ = 0;
  uint8_t slots_[seq::TRACKS] = {0};
  int scenes_ = 0;
  std::vector<uint8_t> arrange_;
};

// false: the song can't be held on the device (*error says why).
bool toDevice(const Song& in, seq::Song& out, Meta& meta, const char** error);
// meta may be null (a song made on the device). name = the song's title online.
bool toWeb(const seq::Song& in, const Meta* meta, const char* name, Song& out, const char** error);

// A small text form of Meta, kept next to each synced song.
void metaToText(const Meta& m, char* out, size_t n);
bool metaFromText(const char* text, Meta& m);

// The device's drum lane note for a General MIDI drum note, or -1 to drop it.
int mapDrumNote(uint8_t gm);

}  // namespace trkr
