// Every case the sync has to get right, including the ones where a wrong
// decision would lose or overwrite a song.
#include <cstdio>
#include <string>

#include "../../src/net/SyncPlan.h"

using namespace cloud;

static int fails = 0;
#define CHECK(cond, what) do { if (!(cond)) { printf("  FAIL: %s\n", what); fails++; } } while (0)

static std::string show(const std::vector<Action>& v) {
  static const char* N[] = {"Download", "Upload", "UploadNew", "DropLocal", "Detach", "Tombstone"};
  std::string s;
  for (auto& a : v) s += std::string(N[(int)a.act]) + "(" + a.name + (a.conflict ? ",conflict" : "") + ") ";
  return s.empty() ? "nothing" : s;
}
static bool is(const std::vector<Action>& v, std::initializer_list<std::pair<Act, const char*>> want) {
  if (v.size() != want.size()) return false;
  size_t i = 0;
  for (auto& w : want) { if (v[i].act != w.first || v[i].name != w.second) return false; i++; }
  return true;
}
static void scenario(const char* name, const std::vector<Action>& got, std::initializer_list<std::pair<Act, const char*>> want) {
  bool ok = is(got, want);
  printf("%s %-58s -> %s\n", ok ? "ok  " : "FAIL", name, show(got).c_str());
  if (!ok) fails++;
}

int main() {
  // A: nothing anywhere
  scenario("empty everywhere", plan({}, {}, {}), {});

  // B: a song appears on the website
  scenario("new on the website", plan({{"a1", 3, false, "City Lights"}}, {}, {}), {{Act::Download, "City Lights"}});
  scenario("new on website, name already used by a device song",
           plan({{"a1", 1, false, "Demo"}}, {{"", "zz", 1, 5}}, {{"Demo", 7}}),
           {{Act::Download, "Demo-2"}, {Act::UploadNew, "Demo"}});

  // C: in step, nothing to do
  scenario("synced and unchanged on both sides",
           plan({{"a1", 3, false, "X"}}, {{"X", "a1", 3, 111}}, {{"X", 111}}), {});

  // D: the website moved on, the device copy untouched -> take theirs
  scenario("website newer, device unchanged",
           plan({{"a1", 4, false, "X"}}, {{"X", "a1", 3, 111}}, {{"X", 111}}), {{Act::Download, "X"}});

  // E: edited on the device, website same version -> send up
  scenario("device edited, website unchanged",
           plan({{"a1", 3, false, "X"}}, {{"X", "a1", 3, 111}}, {{"X", 222}}), {{Act::Upload, "X"}});

  // F: both changed -> NOTHING is lost: device copy kept and sent as new, website copy comes down separately
  auto conflict = plan({{"a1", 4, false, "X"}}, {{"X", "a1", 3, 111}}, {{"X", 222}});
  scenario("BOTH changed: keep both", conflict, {{Act::Detach, "X"}, {Act::Download, "X-2"}, {Act::UploadNew, "X"}});
  CHECK(conflict.size() == 3 && conflict[1].conflict, "download is marked as a conflict copy");

  // G: made on the device
  scenario("made on the device", plan({}, {}, {{"Mine", 5}}), {{Act::UploadNew, "Mine"}});

  // H: deleted on the website
  scenario("website deleted it, device unchanged",
           plan({{"a1", 5, true, ""}}, {{"X", "a1", 3, 111}}, {{"X", 111}}), {{Act::DropLocal, "X"}});
  scenario("website deleted it, device EDITED: keep it",
           plan({{"a1", 5, true, ""}}, {{"X", "a1", 3, 111}}, {{"X", 222}}), {{Act::Detach, "X"}, {Act::UploadNew, "X"}});
  scenario("deleted on the website, never on the device", plan({{"a1", 5, true, ""}}, {}, {}), {});

  // I: deleted on the device must not come back
  scenario("deleted on the device, website unchanged",
           plan({{"a1", 3, false, "X"}}, {{"X", "a1", 3, 111}}, {}), {{Act::Tombstone, "X"}});
  scenario("tombstone remembered: still not fetched",
           plan({{"a1", 3, false, "X"}}, {{"", "a1", 3, 0}}, {}), {});
  scenario("deleted on device, but changed online: it returns",
           plan({{"a1", 4, false, "X"}}, {{"", "a1", 3, 0}}, {}), {{Act::Download, "X"}});

  // J: songs too big for the device
  scenario("skipped song not retried at the same version",
           plan({{"a1", 3, false, "Big"}}, {{"", "a1", 3, 0}}, {}), {});
  scenario("skipped song retried when it changes",
           plan({{"a1", 4, false, "Big"}}, {{"", "a1", 3, 0}}, {}), {{Act::Download, "Big"}});

  // K: several at once, and an unrelated device song stays alone
  scenario("mixed",
           plan({{"a1", 4, false, "A"}, {"b2", 1, false, "B"}}, {{"A", "a1", 3, 10}, {"Keep", "k9", 2, 30}},
                {{"A", 10}, {"Keep", 30}}),
           {{Act::Download, "A"}, {Act::Download, "B"}});

  // L: names
  CHECK(sanitizeName("City Lights!") == "City Lights", "punctuation removed");
  CHECK(sanitizeName("  lo-fi  beat  ") == "lo-fi beat", "spaces trimmed and collapsed");
  CHECK(sanitizeName("A really long song title here") == "A really long so", "cut to 16") ;
  CHECK(sanitizeName("!!!") == "song", "nothing usable -> a default");
  CHECK(sanitizeName("A really long song title here").size() <= 16, "never over 16");
  CHECK(uniqueName({"demo"}, "Demo") == "Demo-2", "case-insensitive clash");
  CHECK(uniqueName({"A really long so", "A really long-2"}, "A really long song") == "A really long-3", "suffix keeps it within 16");
  for (int n = 0; n < 1; n++) {}
  printf("\n%s\n", fails ? "SOME SCENARIOS FAILED" : "all scenarios behave");
  return fails ? 1 : 0;
}
