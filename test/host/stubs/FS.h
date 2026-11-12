// In-memory stand-in for the Arduino filesystem classes.
#pragma once
#include <Arduino.h>

#include <map>
#include <memory>
#include <vector>

namespace fs {

struct Store {
  std::map<std::string, std::string> files;
  std::vector<std::string> dirs;
};

class File {
 public:
  File() {}
  explicit operator bool() const { return store != nullptr; }
  size_t write(uint8_t b) { return write(&b, 1); }
  size_t write(const uint8_t* p, size_t n) {
    if (!store || !writing) { err = 1; return 0; }
    store->files[path].append((const char*)p, n);
    return n;
  }
  int read(uint8_t* b, size_t n) {
    if (!store) return -1;
    const std::string& d = store->files[path];
    size_t k = pos + n <= d.size() ? n : d.size() - pos;
    memcpy(b, d.data() + pos, k);
    pos += k;
    return (int)k;
  }
  void close() {}
  int getWriteError() { return err; }
  String name() const { return String(path); }
  File openNextFile() {
    if (!store || !isDir) return File();
    while (dirPos < listing.size()) {
      File f;
      f.store = store;
      f.path = listing[dirPos++];
      return f;
    }
    return File();
  }
  Store* store = nullptr;
  std::string path;
  size_t pos = 0;
  bool writing = false, isDir = false;
  int err = 0;
  std::vector<std::string> listing;
  size_t dirPos = 0;
};

class FS {
 public:
  Store store;
  bool exists(const String& p) const {
    if (store.files.count(p.s)) return true;
    for (auto& d : store.dirs) if (d == p.s) return true;
    return false;
  }
  bool mkdir(const String& p) { store.dirs.push_back(p.s); return true; }
  bool remove(const String& p) { return store.files.erase(p.s) > 0; }
  bool rename(const String& a, const String& b) {
    auto it = store.files.find(a.s);
    if (it == store.files.end()) return false;
    store.files[b.s] = it->second;
    store.files.erase(a.s);
    return true;
  }
  File open(const String& p, const char* mode = "r") {
    File f;
    for (auto& d : store.dirs) if (d == p.s) {
      f.store = &store; f.isDir = true; f.path = p.s;
      for (auto& kv : store.files) if (kv.first.rfind(p.s + "/", 0) == 0) f.listing.push_back(kv.first);
      return f;
    }
    if (mode[0] == 'w') store.files[p.s] = "";
    else if (!store.files.count(p.s)) return File();
    f.store = &store; f.path = p.s; f.writing = mode[0] == 'w';
    return f;
  }
};

}  // namespace fs
using fs::File;
