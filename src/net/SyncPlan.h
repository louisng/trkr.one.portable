#pragma once
// Decides what a song sync should do. Pure logic (no network, no files), so
// every case can be tested on a PC: a wrong decision here could overwrite or
// lose a song.
//
// The device remembers, for each song it has synced, the website's id, the
// version it was based on, and a checksum of its own file at that moment. A
// song is "changed on the device" when its file's checksum no longer matches.
#include <stdint.h>

#include <string>
#include <vector>

namespace cloud {

struct IdxEntry {
  std::string name;   // song name on the device; empty = a website song the device can't hold (skipped)
  std::string id;     // the website's id
  uint32_t version = 0;
  uint32_t crc = 0;   // checksum of the device's file when last synced
};

struct LocalSong {
  std::string name;
  uint32_t crc = 0;
};

struct RemoteSong {
  std::string id;
  uint32_t version = 0;
  bool deleted = false;
  std::string title;
};

enum class Act : uint8_t {
  Download,      // fetch the website's song into `name` (new, or replacing the device copy)
  Upload,        // send the device's `name` as a new version of the website song `id`
  UploadNew,     // send the device's `name` as a brand-new website song
  DropLocal,     // the website deleted it and it's unchanged here: delete the device copy
  Detach,        // forget the link to the website song; the device copy is kept (and sent as new)
  Tombstone,     // the song was deleted on the device: remember that, don't bring it back unless it changes online
};

struct Action {
  Act act;
  std::string name;
  std::string id;
  uint32_t version = 0;
  std::string title;      // Download: the website's title (the name for a new local copy)
  bool conflict = false;  // changed in both places
};

// ── names ────────────────────────────────────────────────────────────────────
// A device song name: letters, digits, - _ and inner spaces, at most 16.
inline std::string sanitizeName(const std::string& title) {
  std::string out;
  for (char c : title) {
    bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '_';
    if (c == ' ') ok = !out.empty() && out.back() != ' ';
    if (ok && out.size() < 16) out += c;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out.empty() ? "song" : out;
}

inline bool nameTaken(const std::vector<std::string>& taken, const std::string& n) {
  for (const auto& t : taken) {
    if (t.size() != n.size()) continue;
    bool same = true;
    for (size_t i = 0; i < t.size() && same; i++) same = (t[i] | 0x20) == (n[i] | 0x20);  // case-insensitive
    if (same) return true;
  }
  return false;
}

// A name not in `taken`: the base itself, or base-2, base-3 ... (still <= 16 characters).
inline std::string uniqueName(const std::vector<std::string>& taken, const std::string& title) {
  std::string base = sanitizeName(title);
  if (!nameTaken(taken, base)) return base;
  for (int n = 2; n < 1000; n++) {
    std::string suffix = "-" + std::to_string(n);
    std::string cand = base.substr(0, 16 - suffix.size());
    while (!cand.empty() && cand.back() == ' ') cand.pop_back();
    cand += suffix;
    if (!nameTaken(taken, cand)) return cand;
  }
  return base;
}

// ── the plan ─────────────────────────────────────────────────────────────────
inline std::vector<Action> plan(const std::vector<RemoteSong>& remote, const std::vector<IdxEntry>& index,
                                const std::vector<LocalSong>& local) {
  std::vector<Action> out;
  std::vector<std::string> taken;  // names in use, including ones this plan hands out
  for (const auto& l : local) taken.push_back(l.name);
  std::vector<std::string> detached;  // device songs that lose their website link: sent up as new

  auto findLocal = [&](const std::string& name) -> const LocalSong* {
    for (const auto& l : local)
      if (l.name == name) return &l;
    return nullptr;
  };
  auto newName = [&](const std::string& title) {
    std::string n = uniqueName(taken, title);
    taken.push_back(n);
    return n;
  };

  // A song deleted on the device is remembered, so the sync doesn't fetch it again.
  for (const auto& e : index)
    if (!e.name.empty() && !findLocal(e.name)) out.push_back({Act::Tombstone, e.name, e.id, e.version, "", false});

  for (const auto& r : remote) {
    const IdxEntry* e = nullptr;
    for (const auto& x : index)
      if (x.id == r.id) { e = &x; break; }

    if (!e) {  // never seen before
      if (!r.deleted) out.push_back({Act::Download, newName(r.title), r.id, r.version, r.title, false});
      continue;
    }
    const LocalSong* l = e->name.empty() ? nullptr : findLocal(e->name);
    if (!l) {  // skipped (too big) or deleted on the device: only fetch again if it changed online
      if (!r.deleted && r.version > e->version)
        out.push_back({Act::Download, newName(r.title), r.id, r.version, r.title, false});
      continue;
    }
    const bool changedHere = l->crc != e->crc;
    if (r.deleted) {
      if (changedHere) {  // deleted online but edited here: keep their edits
        out.push_back({Act::Detach, e->name, r.id, 0, "", true});
        detached.push_back(e->name);
      } else {
        out.push_back({Act::DropLocal, e->name, r.id, 0, "", false});
      }
      continue;
    }
    if (r.version > e->version) {
      if (!changedHere) {
        out.push_back({Act::Download, e->name, r.id, r.version, r.title, false});
      } else {
        // Changed in both places: the device copy is kept and sent up as a new
        // song, and the website's version comes down as a separate copy.
        out.push_back({Act::Detach, e->name, r.id, 0, "", true});
        detached.push_back(e->name);
        out.push_back({Act::Download, newName(r.title), r.id, r.version, r.title, true});
      }
    } else if (changedHere && r.version == e->version) {
      out.push_back({Act::Upload, e->name, r.id, e->version, "", false});
    }
  }

  // Songs made on the device that the website has never seen (or just lost their link).
  for (const auto& l : local) {
    bool known = false;
    for (const auto& e : index)
      if (e.name == l.name && !e.id.empty()) known = true;
    for (const auto& d : detached)
      if (d == l.name) known = false;
    if (!known) out.push_back({Act::UploadNew, l.name, "", 0, "", false});
  }
  return out;
}

}  // namespace cloud
