#include "GenTheory.h"

#include <math.h>
#include <string.h>

#include <algorithm>

namespace gen {

static const char* const SHARP[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char* const FLAT[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
const char* noteName(int pc, bool flats) { return (flats ? FLAT : SHARP)[mod12(pc)]; }

bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

static const uint8_t SCALES[7][7] = {
    {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
    {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 2, 3, 5, 7, 8, 11},
};

int scaleOffset(int scale, int degree) {
  int oct = (int)floor(degree / 7.0);
  int d = degree - oct * 7;
  return SCALES[((scale % 7) + 7) % 7][d] + oct * 12;
}
bool isMinorFamily(int scale) { return scaleOffset(scale, 2) == 3; }

std::vector<int> scalePcs(int root, int scale) {
  std::vector<int> out;
  for (int i = 0; i < 7; i++) out.push_back((root + SCALES[scale % 7][i]) % 12);
  return out;
}

// ── chord symbols ────────────────────────────────────────────────────────────
struct QRow {
  const char* suffix;
  uint8_t n;
  int8_t tones[6];
  Quality q;
};
// Longest suffixes first, same table as theory.js (ASCII entries).
static const QRow QUALITY_TABLE[] = {
    {"maj13", 6, {0, 4, 7, 11, 14, 21}, Q_MAJ}, {"maj9", 5, {0, 4, 7, 11, 14}, Q_MAJ},
    {"maj7#11", 5, {0, 4, 7, 11, 18}, Q_MAJ},   {"maj7", 4, {0, 4, 7, 11}, Q_MAJ},
    {"M7", 4, {0, 4, 7, 11}, Q_MAJ},            {"mMaj7", 4, {0, 3, 7, 11}, Q_MIN},
    {"m(maj7)", 4, {0, 3, 7, 11}, Q_MIN},       {"m7b5", 4, {0, 3, 6, 10}, Q_HDIM},
    {"m13", 6, {0, 3, 7, 10, 14, 21}, Q_MIN},   {"m11", 6, {0, 3, 7, 10, 14, 17}, Q_MIN},
    {"m9", 5, {0, 3, 7, 10, 14}, Q_MIN},        {"m7", 4, {0, 3, 7, 10}, Q_MIN},
    {"m69", 5, {0, 3, 7, 9, 14}, Q_MIN},        {"m6", 4, {0, 3, 7, 9}, Q_MIN},
    {"madd9", 4, {0, 3, 7, 14}, Q_MIN},         {"min7", 4, {0, 3, 7, 10}, Q_MIN},
    {"min", 3, {0, 3, 7}, Q_MIN},               {"-7", 4, {0, 3, 7, 10}, Q_MIN},
    {"m", 3, {0, 3, 7}, Q_MIN},                 {"-", 3, {0, 3, 7}, Q_MIN},
    {"dim7", 4, {0, 3, 6, 9}, Q_DIM},           {"dim", 3, {0, 3, 6}, Q_DIM},
    {"o7", 4, {0, 3, 6, 9}, Q_DIM},             {"o", 3, {0, 3, 6}, Q_DIM},
    {"aug7", 4, {0, 4, 8, 10}, Q_AUG},          {"aug", 3, {0, 4, 8}, Q_AUG},
    {"+", 3, {0, 4, 8}, Q_AUG},                 {"7sus4", 4, {0, 5, 7, 10}, Q_SUS},
    {"9sus4", 5, {0, 5, 7, 10, 14}, Q_SUS},     {"sus4", 3, {0, 5, 7}, Q_SUS},
    {"sus2", 3, {0, 2, 7}, Q_SUS},              {"sus", 3, {0, 5, 7}, Q_SUS},
    {"13", 6, {0, 4, 7, 10, 14, 21}, Q_DOM},    {"11", 5, {0, 7, 10, 14, 17}, Q_DOM},
    {"9", 5, {0, 4, 7, 10, 14}, Q_DOM},         {"7b9", 5, {0, 4, 7, 10, 13}, Q_DOM},
    {"7#9", 5, {0, 4, 7, 10, 15}, Q_DOM},       {"7", 4, {0, 4, 7, 10}, Q_DOM},
    {"69", 5, {0, 4, 7, 9, 14}, Q_MAJ},         {"6", 4, {0, 4, 7, 9}, Q_MAJ},
    {"add9", 4, {0, 4, 7, 14}, Q_MAJ},          {"maj", 3, {0, 4, 7}, Q_MAJ},
    {"", 3, {0, 4, 7}, Q_MAJ},
};

static bool parseNoteLetter(char c, int& pc) {
  switch (c) {
    case 'C': pc = 0; return true;
    case 'D': pc = 2; return true;
    case 'E': pc = 4; return true;
    case 'F': pc = 5; return true;
    case 'G': pc = 7; return true;
    case 'A': pc = 9; return true;
    case 'B': pc = 11; return true;
  }
  return false;
}

bool parseChord(const std::string& raw, Chord& out) {
  if (raw.empty()) return false;
  int root;
  if (!parseNoteLetter(raw[0], root)) return false;
  size_t i = 1;
  if (i < raw.size() && raw[i] == '#') { root++; i++; }
  else if (i < raw.size() && raw[i] == 'b') { root--; i++; }
  root = mod12(root);
  std::string rest = raw.substr(i);
  int bass = root;
  size_t slash = rest.rfind('/');
  if (slash != std::string::npos) {
    std::string b = rest.substr(slash + 1);
    int pc;
    if (!b.empty() && parseNoteLetter(b[0], pc)) {
      if (b.size() > 1 && b[1] == '#') pc++;
      else if (b.size() > 1 && b[1] == 'b') pc--;
      bass = mod12(pc);
    }
    rest = rest.substr(0, slash);
  }
  if (rest == "+9") rest = "add9";
  else if (rest.size() >= 2 && rest.compare(rest.size() - 2, 2, "+9") == 0) rest = rest.substr(0, rest.size() - 2) + "9";
  out = Chord();
  out.root = root;
  out.bass = bass;
  out.symbol = raw;
  for (const QRow& row : QUALITY_TABLE) {
    if (rest == row.suffix) {
      out.tones.assign(row.tones, row.tones + row.n);
      out.quality = row.q;
      return true;
    }
  }
  bool minor = (rest.rfind("m", 0) == 0 || rest.rfind("min", 0) == 0 || rest.rfind("-", 0) == 0) && rest.rfind("maj", 0) != 0;
  out.tones = minor ? std::vector<int>{0, 3, 7, 10} : std::vector<int>{0, 4, 7};
  out.quality = minor ? Q_MIN : Q_MAJ;
  return true;
}

// ── Roman numerals ───────────────────────────────────────────────────────────
static const char* const ROMAN_ORDER[] = {"VII", "VI", "V", "IV", "III", "II", "I", "vii", "vi", "v", "iv", "iii", "ii", "i"};
static int romanDegree(const std::string& num) {
  std::string l;
  for (char c : num) l += (char)tolower(c);
  static const char* const names[] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
  for (int i = 0; i < 7; i++) if (l == names[i]) return i;
  return 0;
}

bool romanToChord(const std::string& roman, int keyRoot, int keyScale, Chord& out) {
  size_t p = 0;
  char acc = 0;
  if (p < roman.size() && (roman[p] == 'b' || roman[p] == '#')) { acc = roman[p]; p++; }
  std::string num;
  for (const char* r : ROMAN_ORDER) {
    size_t n = strlen(r);
    if (roman.compare(p, n, r) == 0) { num = r; break; }
  }
  if (num.empty()) {
    // "b" or "#" may be part of nothing else here, so there is no match.
    return false;
  }
  p += num.size();
  std::string suffix, target;
  size_t slash = roman.find('/', p);
  if (slash == std::string::npos) suffix = roman.substr(p);
  else { suffix = roman.substr(p, slash - p); target = roman.substr(slash + 1); }

  int base = keyRoot;
  bool targetIsDigit = target.size() == 1 && isdigit((unsigned char)target[0]);
  if (!target.empty() && !targetIsDigit) {
    Chord t;
    if (romanToChord(target, keyRoot, keyScale, t)) base = t.root;
  }
  int deg = romanDegree(num);
  int root = mod12(base + SCALES[0][deg] + (acc == 'b' ? -1 : acc == '#' ? 1 : 0));
  bool upper = true;
  for (char c : num) if (c != toupper((unsigned char)c)) upper = false;

  std::string sym;
  auto starts = [&](const char* s) { return suffix.rfind(s, 0) == 0; };
  if (starts("o") || starts("dim")) {
    sym = suffix;
    if (starts("o")) sym = "dim" + suffix.substr(1);
  } else if (starts("m7b5")) {
    sym = "m7b5";
  } else if (starts("+") || starts("aug")) {
    sym = "aug" + suffix.substr(starts("+") ? 1 : 3);
  } else if (upper) {
    sym = suffix;
  } else if (starts("maj")) {
    sym = "mMaj7";
  } else {
    sym = "m" + suffix;
  }
  if (sym == "dim" && suffix.find('7') != std::string::npos) sym = "dim7";

  Chord c;
  if (!parseChord(std::string(noteName(root)) + sym, c)) parseChord(noteName(root), c);
  static const bool FLAT_MAJOR_KEYS[12] = {0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0};  // 1 3 5 6 8 10
  int relMajor = isMinorFamily(keyScale) ? mod12(keyRoot + 3) : mod12(keyRoot);
  bool flats = acc == 'b' || (acc != '#' && FLAT_MAJOR_KEYS[relMajor]);
  c.symbol = std::string(noteName(root, flats)) + sym;
  if (targetIsDigit) {
    int idx = target[0] == '3' ? 1 : target[0] == '5' ? 2 : target[0] == '7' ? 3 : -1;
    if (idx >= 0 && idx < (int)c.tones.size()) {
      c.bass = mod12(c.root + c.tones[idx]);
      c.symbol += std::string("/") + noteName(c.bass, flats);
    }
  }
  out = c;
  return true;
}

// ── scales, tones ────────────────────────────────────────────────────────────
std::vector<int> chordPcs(const Chord& c) {
  std::vector<int> out;
  for (int t : c.tones) {
    int pc = mod12(c.root + t);
    if (!contains(out, pc)) out.push_back(pc);
  }
  return out;
}

std::vector<int> chordScale(const Chord& c, int keyRoot, int keyScale) {
  std::vector<int> keyPcs = scalePcs(keyRoot, keyScale);
  bool all = true;
  for (int pc : chordPcs(c)) if (!contains(keyPcs, pc)) all = false;
  if (all) {
    auto it = std::find(keyPcs.begin(), keyPcs.end(), c.root);
    if (it != keyPcs.end()) {
      std::vector<int> out(it, keyPcs.end());
      out.insert(out.end(), keyPcs.begin(), it);
      return out;
    }
  }
  static const std::vector<int> BY_Q[] = {
      {0, 2, 4, 6, 7, 9, 11},     // maj
      {0, 2, 3, 5, 7, 9, 10},     // min
      {0, 2, 4, 5, 7, 9, 10},     // dom
      {0, 1, 3, 5, 6, 8, 10},     // hdim
      {0, 2, 3, 5, 6, 8, 9, 11},  // dim
      {0, 2, 4, 6, 8, 10},        // aug
      {0, 2, 5, 7, 9, 10},        // sus
  };
  std::vector<int> out;
  for (int s : BY_Q[c.quality]) out.push_back(mod12(c.root + s));
  return out;
}

std::vector<int> avoidPcs(const Chord& c, const std::vector<int>& scale) {
  std::vector<int> tones = chordPcs(c), out;
  for (int pc : scale)
    if (contains(tones, mod12(pc - 1)) && !contains(tones, pc)) out.push_back(pc);
  return out;
}

int nearestPc(int pc, int near, int lo, int hi) {
  bool have = false;
  int best = 0, bestD = 1 << 30;
  for (int o = -2; o <= 2; o++) {
    int x = near - mod12(near) + mod12(pc) + o * 12;
    if (x < lo || x > hi) continue;
    int d = abs(x - near);
    if (d < bestD) { bestD = d; best = x; have = true; }
  }
  if (have) return best;
  return std::max(lo, std::min(hi, near));
}

// ── voicing & voice leading ──────────────────────────────────────────────────
std::vector<int> voiceChord(const Chord& c, const std::vector<int>* prev, int lo, int hi,
                            const std::string& style, int maxNotes) {
  std::vector<int> tones = c.tones;
  bool has7 = false;
  bool colors = false;
  for (int t : tones) {
    if (t == 10 || t == 11 || (t == 9 && c.quality == Q_DIM)) has7 = true;
    if (t > 12) colors = true;
  }
  if (style == "rootless" && (has7 || colors)) {
    tones.erase(std::remove(tones.begin(), tones.end(), 0), tones.end());
  }
  if ((int)tones.size() > maxNotes) {
    static const int drop[] = {7, 0, 14};
    for (int d : drop) {
      if ((int)tones.size() <= maxNotes) break;
      auto it = std::find(tones.begin(), tones.end(), d);
      if (it != tones.end() && !(d == 7 && c.quality == Q_AUG)) tones.erase(it);
    }
    if ((int)tones.size() > maxNotes) tones.resize(maxNotes);
  }
  std::vector<int> pcs;
  for (int t : tones) pcs.push_back(mod12(c.root + t));

  std::vector<std::vector<int>> candidates;
  int n = (int)pcs.size();
  bool dropStyle = style == "drop2" || style == "spread";
  for (int rot = 0; rot < n; rot++) {
    std::vector<int> order;
    for (int i = 0; i < n; i++) order.push_back(pcs[(rot + i) % n]);
    for (int base = lo - 12; base <= hi; base++) {
      if (mod12(base) != order[0]) continue;
      std::vector<int> v{base};
      for (int i = 1; i < n; i++) {
        int x = v[i - 1] + 1;
        while (mod12(x) != order[i]) x++;
        v.push_back(x);
      }
      std::vector<std::vector<int>> variants{v};
      if (dropStyle && n >= 4) {
        std::vector<int> d = v;
        d[n - 2] -= 12;
        std::sort(d.begin(), d.end());
        variants.push_back(d);
      }
      for (auto& cand : variants)
        if (cand.front() >= lo && cand.back() <= hi) candidates.push_back(cand);
    }
  }
  if (candidates.empty()) {
    int x = lo;
    std::vector<int> out;
    for (int pc : pcs) {
      while (mod12(x) != pc) x++;
      out.push_back(x);
      x++;
    }
    return out;
  }
  double center = (lo + hi) / 2.0;
  size_t best = 0;
  double bestCost = INFINITY;
  for (size_t ci = 0; ci < candidates.size(); ci++) {
    const auto& cand = candidates[ci];
    double sum = 0;
    for (int x : cand) sum += x;
    double cost;
    if (prev && !prev->empty()) {
      double s = 0;
      for (int x : cand) {
        int m = 1 << 30;
        for (int p : *prev) m = std::min(m, abs(p - x));
        s += m;
      }
      cost = s;
      cost += fabs(sum / cand.size() - center) * 0.35;
    } else {
      cost = fabs(sum / cand.size() - center);
    }
    if (cand.size() > 1 && cand[1] - cand[0] <= 2 && cand[0] < 55) cost += 4;
    if (cost < bestCost) { bestCost = cost; best = ci; }
  }
  return candidates[best];
}

}  // namespace gen
