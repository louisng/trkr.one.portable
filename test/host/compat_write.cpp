// Saves one fixed song with whatever song code it is built against, and dumps
// the file: building it against the ORIGINAL firmware's song code and the new
// one must give identical bytes (so older firmware can still read the songs).
#include <Arduino.h>
#include <LittleFS.h>

#include <fstream>
#include <iterator>

#include "seq/Song.h"

LittleFSClass LittleFS;
namespace seq {
fs::FS* sdFs() { return nullptr; }
}  // namespace seq

int main(int argc, char** argv) {
  using namespace seq;
  if (argc > 2) {  // read a file written by other code, save it again, dump it
    std::ifstream in(argv[2], std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    LittleFS.store.dirs.push_back("/songs");
    LittleFS.store.files["/songs/compat.cms"] = bytes;
    if (!songLoadNamed("compat")) return 3;
    if (!songSaveAs("again")) return 4;
    std::ofstream(argv[1], std::ios::binary) << LittleFS.store.files["/songs/again.cms"];
    return 0;
  }
  songInit();
  song.bpm = 133;
  song.songMode = true;
  song.arrange = {0, 1, 1, 2, 0};
  strcpy(song.tracks[1].name, "Bass");
  song.tracks[1].channel = 5;
  song.tracks[2].mute = true;
  song.tracks[3].hold = true;
  for (uint8_t t = 0; t < TRACKS; t++)
    for (uint8_t p = 0; p < 8; p++) {
      song.tracks[t].patterns[p].length = (uint8_t)(8 + 4 * p);
      for (uint8_t st = 0; st < song.tracks[t].patterns[p].length; st += 3) {
        uint8_t pitch = (uint8_t)(36 + (t * 7 + p * 3 + st) % 40);
        setStep(song.tracks[t].patterns[p], st, &pitch, 1, (uint8_t)(60 + st), (uint16_t)(6 + p));
      }
      if (p == 2) setFx(song.tracks[t].patterns[p], 1, FX_CHANCE, 50);
    }
  for (uint8_t sc = 0; sc < 3; sc++)
    for (uint8_t t = 0; t < TRACKS; t++) song.scenes[sc].pattern[t] = (t + sc) % 8;
  song.scenes[1].pattern[4] = SCENE_SILENT;
  if (!songSaveAs("compat")) return 2;
  std::ofstream(argv[1], std::ios::binary) << LittleFS.store.files["/songs/compat.cms"];
  return 0;
}
