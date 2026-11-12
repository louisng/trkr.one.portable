#include "Cloud.h"

#include "../app/Settings.h"
#include "../midi/MidiOut.h"
#include "../seq/Song.h"
#include "SyncPlan.h"
#include "TrkrConvert.h"
#include "TrkrSong.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <NetworkClientSecure.h>
#include <esp_heap_caps.h>
#include <WiFi.h>
#include <string.h>

#include <vector>

// Root certificates that ship with ESP-IDF (the Mozilla bundle), so the
// device checks it is really talking to trkr.one.
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace cloud {

static const char* DEFAULT_HOST = "trkr.one";

static volatile State st = State::Idle;
static char msg[64] = "";
static char detailBuf[64] = "";
static uint16_t bleFreedKb = 0;
static volatile bool pending = false, fin = false, uiRestored = false;
static char code[16] = "";
static char url[48] = "";
static char cfgHost[40] = "";
#define cfgSsid (settings.wifiSsid)
#define cfgPass (settings.wifiPass)
#define cfgToken (settings.cloudToken)
#define cfgEmail (settings.cloudEmail)
static volatile bool cancelReq = false;
static TaskHandle_t task = nullptr;
static uint32_t syncedAt = 0;
static uint16_t nDown = 0, nUp = 0;

enum class Job : uint8_t { Link, Login, Sync };
static char loginEmail[64] = "", loginPass[72] = "";
static Job job;

static void setMsg(const char* m) {
  strncpy(msg, m, sizeof(msg) - 1);
  msg[sizeof(msg) - 1] = 0;
}

void begin() { strcpy(cfgHost, DEFAULT_HOST); }
State state() { return st; }
const char* message() { return msg; }
const char* detail() { return detailBuf; }
bool needsMemory() { return pending; }
bool finishing() { return fin; }
void restored() { uiRestored = true; }
int bleFreedLastKb() { return bleFreedKb; }
const char* linkCode() { return code; }
const char* linkUrl() { return url; }
bool hasWifi() { return cfgSsid[0] != 0; }
const char* ssid() { return cfgSsid; }
int passLen() { return (int)strlen(cfgPass); }
bool linked() { return cfgToken[0] != 0; }
const char* email() { return cfgEmail; }
const char* host() { return cfgHost; }
bool busy() { return task != nullptr || pending; }
void cancel() { cancelReq = true; }
uint32_t lastSyncMs() { return syncedAt; }
uint16_t lastDownloaded() { return nDown; }
uint16_t lastUploaded() { return nUp; }

// ── scanning ─────────────────────────────────────────────────────────────────
static std::vector<Net> nets;
static bool scanActive = false, scanBad = false, radioForScan = false;
static uint32_t scanStarted = 0;

void startScan() {
  if (task || scanActive) return;  // the radio is busy with a sync / link
  nets.clear();
  scanBad = false;
  radioForScan = true;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.scanDelete();
  if (WiFi.scanNetworks(true /*async*/, false /*skip hidden*/) == WIFI_SCAN_FAILED) {
    scanBad = true;
    return;
  }
  scanActive = true;
  scanStarted = millis();
}

bool scanning() {
  if (!scanActive) return false;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    if (millis() - scanStarted > 15000) {  // gave up waiting
      scanActive = false;
      scanBad = true;
    }
    return scanActive;
  }
  scanActive = false;
  if (n < 0) {
    scanBad = true;
    return false;
  }
  for (int i = 0; i < n; i++) addNet(nets, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN, (uint8_t)WiFi.encryptionType(i));
  sortNets(nets);
  WiFi.scanDelete();
  return false;
}

bool scanFailed() { return scanBad; }
int scanCount() { return (int)nets.size(); }
const Net& scanNet(int i) { return nets[i]; }

void endScan() {
  if (!radioForScan) return;   // we never turned the radio on: leave WiFi alone
  radioForScan = false;
  scanActive = false;
  WiFi.scanDelete();
  if (!task) {  // don't pull the radio from under a running sync
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
}

void setWifi(const char* s, const char* pw) {
  strncpy(cfgSsid, s, sizeof(cfgSsid) - 1);
  strncpy(cfgPass, pw, sizeof(cfgPass) - 1);
}

void unlink() {
  cfgToken[0] = 0;
  cfgEmail[0] = 0;
}

// ── HTTP helpers ─────────────────────────────────────────────────────────────
static volatile uint8_t lastReason = 0;  // why the radio last dropped or refused us

static void onWifiEvent(arduino_event_id_t, arduino_event_info_t info) {
  lastReason = info.wifi_sta_disconnected.reason;
}

static const char* reasonText(uint8_t r) {
  switch (r) {
    case 201: return "network not found";
    case 202: case 15: case 204: case 2: return "wrong password?";
    case 205: return "connect failed";
    case 203: return "assoc failed";
    case 8: case 3: return "dropped by router";
    default: return "no answer";
  }
}

static bool waitJoined(uint32_t ms) {
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (cancelReq || millis() - t0 > ms) return false;
    delay(100);
  }
  return true;
}

// Joins the saved network. It first looks the network up (so we know it is
// really in range, its channel and security), then tries up to three ways of
// joining. The status line says what happened; the detail line says what was
// seen and what each try ended with.
static bool wifiUp() {
  if (WiFi.status() == WL_CONNECTED) return true;
  static bool hooked = false;
  if (!hooked) {
    WiFi.onEvent(onWifiEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    hooked = true;
  }
  detailBuf[0] = 0;
  st = State::Connecting;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  setMsg("Looking for network...");
  int found = WiFi.scanNetworks(false, false, false, 300, 0, cfgSsid);
  if (cancelReq) return false;
  if (found <= 0) {
    WiFi.scanDelete();
    setMsg("WiFi: network not in range");
    snprintf(detailBuf, sizeof(detailBuf), "looked for \"%.30s\"", cfgSsid);
    return false;
  }
  int best = 0;
  for (int i = 1; i < found; i++)
    if (WiFi.RSSI(i) > WiFi.RSSI(best)) best = i;
  int ch = WiFi.channel(best);
  uint8_t bssid[6];
  memcpy(bssid, WiFi.BSSID(best), 6);
  int rssi = WiFi.RSSI(best);
  uint8_t auth = (uint8_t)WiFi.encryptionType(best);
  WiFi.scanDelete();

  uint8_t reasons[3] = {0, 0, 0};
  for (int attempt = 0; attempt < 3; attempt++) {
    lastReason = 0;
    snprintf(msg, sizeof(msg), "Joining WiFi (try %d/3)...", attempt + 1);
    snprintf(detailBuf, sizeof(detailBuf), "ch%d %s %ddBm pw %d", ch, authName(auth), rssi, (int)strlen(cfgPass));
    WiFi.disconnect();
    delay(150);
    if (attempt == 0) {
      WiFi.begin(cfgSsid, cfgPass);
    } else {
      if (attempt == 2) WiFi.setTxPower(WIFI_POWER_11dBm);  // a gentler transmitter
      WiFi.begin(cfgSsid, cfgPass, ch, bssid);
    }
    if (waitJoined(12000)) return true;
    if (cancelReq) return false;
    reasons[attempt] = lastReason;
  }
  snprintf(msg, sizeof(msg), "WiFi: %s (%u)", reasonText(reasons[2]), (unsigned)reasons[2]);
  snprintf(detailBuf, sizeof(detailBuf), "ch%d %s %d t%u/%u/%u heap %uk/%uk min %uk", ch, authName(auth), rssi,
           reasons[0], reasons[1], reasons[2], (unsigned)(esp_get_free_heap_size() / 1024),
           (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024),
           (unsigned)(esp_get_minimum_free_heap_size() / 1024));
  return false;
}

static void wifiDown() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// A secure connection checks the server's certificate dates against the clock,
// and this device has none until it asks the internet. Without this, every
// HTTPS request fails ("not yet valid") with HTTP -1.
static bool syncClock() {
  if (time(nullptr) > 1700000000) return true;  // already set
  setMsg("Setting the clock...");
  configTime(0, 0, "pool.ntp.org", "time.cloudflare.com", "time.google.com");
  for (uint32_t t0 = millis(); millis() - t0 < 12000;) {
    if (cancelReq) return false;
    if (time(nullptr) > 1700000000) return true;
    delay(100);
  }
  setMsg("No internet time (NTP)");
  snprintf(detailBuf, sizeof(detailBuf), "can't reach a time server");
  return false;
}

// Make sure the server's name can be looked up before talking to it. If the
// network's own DNS server can't do it, fall back to public ones.
static bool resolveHost() {
  IPAddress ip;
  setMsg("Finding the server...");
  if (WiFi.hostByName(cfgHost, ip) == 1) return true;
  WiFi.setDNS(IPAddress(1, 1, 1, 1), IPAddress(8, 8, 8, 8));
  delay(200);
  if (WiFi.hostByName(cfgHost, ip) == 1) return true;
  snprintf(msg, sizeof(msg), "Can't find %s (DNS)", cfgHost);
  IPAddress me = WiFi.localIP(), dns = WiFi.dnsIP();
  snprintf(detailBuf, sizeof(detailBuf), "st %d ip %u.%u.%u.%u dns %u.%u.%u.%u", (int)WiFi.status(), me[0], me[1], me[2],
           me[3], dns[0], dns[1], dns[2], dns[3]);
  return false;
}

struct Http {
  NetworkClientSecure tls;
  HTTPClient http;
  Http() {
    tls.setCACertBundle(x509_crt_bundle_start, x509_crt_bundle_end - x509_crt_bundle_start);
    http.setReuse(false);
    http.setTimeout(15000);
  }
  // After a failed request: say why (the library's own TLS error text).
  void note(int rc) {
    char eb[96] = "";
    int e = tls.lastError(eb, sizeof(eb));
    // Which step broke? Look the name up, then try a plain connection.
    IPAddress ip;
    bool dns = WiFi.hostByName(cfgHost, ip) == 1;
    bool tcp = false;
    if (dns) {
      WiFiClient plain;
      tcp = plain.connect(ip, 443, 6000);
      plain.stop();
    }
    snprintf(detailBuf, sizeof(detailBuf), "dns %s tcp %s tls %d", dns ? "ok" : "FAIL", dns ? (tcp ? "ok" : "FAIL") : "-", e);
  }
  bool open(const String& path, bool auth) {
    String u = String("https://") + cfgHost + path;
    if (!http.begin(tls, u)) return false;
    http.addHeader("Accept", "text/plain");
    http.setUserAgent("trkr-cardputer/1");
    if (auth) http.addHeader("Authorization", String("Bearer ") + cfgToken);
    return true;
  }
};

// Parse "key=value" lines.
static String kv(const String& body, const char* key) {
  String k = String(key) + "=";
  int i = body.startsWith(k) ? 0 : body.indexOf("\n" + k);
  if (i < 0) return "";
  if (i > 0) i++;
  int s = i + k.length();
  int e = body.indexOf('\n', s);
  return body.substring(s, e < 0 ? body.length() : e);
}

// ── link ─────────────────────────────────────────────────────────────────────
static bool doLink() {
  code[0] = 0;
  String deviceCode;
  int interval = 5;
  uint32_t expires = 0;
  {
    Http h;
    if (!h.open("/api/devices/code", false)) { setMsg("Can't reach server"); return false; }
    h.http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int rc = h.http.POST("label=Cardputer");
    if (rc < 0) h.note(rc);
    String body = h.http.getString();
    h.http.end();
    if (rc != 200) { snprintf(msg, sizeof(msg), "Link failed (HTTP %d)", rc); return false; }
    deviceCode = kv(body, "device_code");
    strncpy(code, kv(body, "user_code").c_str(), sizeof(code) - 1);
    snprintf(url, sizeof(url), "%s/link", cfgHost);
    interval = max(2, (int)kv(body, "interval").toInt());
    expires = millis() + kv(body, "expires_in").toInt() * 1000UL;
  }
  st = State::Linking;
  setMsg("Enter the code at trkr.one/link");
  while ((int32_t)(expires - millis()) > 0) {
    for (int i = 0; i < interval * 10; i++) {
      if (cancelReq) return false;
      delay(100);
    }
    Http h;
    if (!h.open("/api/devices/token", false)) continue;
    h.http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int rc = h.http.POST(String("device_code=") + deviceCode);
    if (rc < 0) h.note(rc);
    String body = h.http.getString();
    h.http.end();
    if (rc == 200) {
      strncpy(cfgToken, kv(body, "token").c_str(), sizeof(cfgToken) - 1);
      strncpy(cfgEmail, kv(body, "email").c_str(), sizeof(cfgEmail) - 1);
      code[0] = 0;
      snprintf(msg, sizeof(msg), "Linked to %s", cfgEmail);
      return true;
    }
    if (kv(body, "error") != "authorization_pending") break;
  }
  code[0] = 0;
  setMsg("Code expired - try again");
  return false;
}

// ── sign in with email + password ────────────────────────────────────────────
static String urlEncode(const char* s) {
  String out;
  for (; *s; s++) {
    char c = *s;
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') out += c;
    else { char b[4]; snprintf(b, sizeof(b), "%%%02X", (unsigned char)c); out += b; }
  }
  return out;
}

static bool doLogin() {
  st = State::Linking;
  setMsg("Signing in...");
  Http h;
  if (!h.open("/api/auth/device-login", false)) { setMsg("Can't reach server"); return false; }
  h.http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String body = String("email=") + urlEncode(loginEmail) + "&password=" + urlEncode(loginPass) + "&label=Cardputer";
  memset(loginPass, 0, sizeof(loginPass));
  int rc = h.http.POST(body);
  if (rc < 0) h.note(rc);
  body = "";
  String resp = h.http.getString();
  h.http.end();
  if (rc == 200) {
    strncpy(cfgToken, kv(resp, "token").c_str(), sizeof(cfgToken) - 1);
    strncpy(cfgEmail, kv(resp, "email").c_str(), sizeof(cfgEmail) - 1);
    snprintf(msg, sizeof(msg), "Signed in as %s", cfgEmail);
    return true;
  }
  if (rc == 401) setMsg("Wrong email or password");
  else if (rc == 429) setMsg("Too many tries - wait 15 min");
  else snprintf(msg, sizeof(msg), "Sign-in failed (HTTP %d)", rc);
  return false;
}

// ── sync ─────────────────────────────────────────────────────────────────────
// Songs are matched between the device and trkr.one by the website's song id,
// kept in /cloud.idx (one line per song: name, id, version, checksum of the
// device's file when it was last synced). What to do is decided by plan()
// in SyncPlan.h, which is tested on a PC; this part carries it out.
// A song that came from the website also has /songs/<name>.web with the few
// things the device doesn't keep (swing, key, full track names, title), so a
// song edited on the device goes back with them intact.
static const char* IDX_PATH = "/cloud.idx";
static const char* TMP_DOWN = "/cloud_dl.tmp";
static const char* TMP_UP = "/cloud_up.tmp";

static String cmsPath(const std::string& name) { return String("/songs/") + name.c_str() + ".cms"; }
static String webPath(const std::string& name) { return String("/songs/") + name.c_str() + ".web"; }

static uint32_t crc32File(const String& path) {
  File f = LittleFS.open(path, "r");
  if (!f) return 0;
  uint32_t crc = 0xFFFFFFFFu;
  uint8_t buf[256];
  for (;;) {
    int n = f.read(buf, sizeof(buf));
    if (n <= 0) break;
    for (int i = 0; i < n; i++) {
      crc ^= buf[i];
      for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
  }
  f.close();
  return ~crc;
}

static std::vector<cloud::IdxEntry> loadIndex() {
  std::vector<cloud::IdxEntry> v;
  File f = LittleFS.open(IDX_PATH, "r");
  if (!f) return v;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int a = line.indexOf('\t'), b = line.indexOf('\t', a + 1), c = line.indexOf('\t', b + 1);
    if (a < 0 || b < 0 || c < 0) continue;
    cloud::IdxEntry e;
    e.name = line.substring(0, a).c_str();
    e.id = line.substring(a + 1, b).c_str();
    e.version = (uint32_t)line.substring(b + 1, c).toInt();
    e.crc = (uint32_t)strtoul(line.substring(c + 1).c_str(), nullptr, 16);
    if (!e.id.empty()) v.push_back(e);
  }
  f.close();
  return v;
}

static void saveIndex(const std::vector<cloud::IdxEntry>& v) {
  File f = LittleFS.open("/cloud.tmp", "w");
  if (!f) return;
  for (const auto& e : v) f.printf("%s\t%s\t%u\t%08x\n", e.name.c_str(), e.id.c_str(), (unsigned)e.version, (unsigned)e.crc);
  f.close();
  LittleFS.remove(IDX_PATH);
  LittleFS.rename("/cloud.tmp", IDX_PATH);
}

static void setEntry(std::vector<cloud::IdxEntry>& v, const cloud::IdxEntry& e) {
  for (auto& x : v)
    if (x.id == e.id) { x = e; return; }
  v.push_back(e);
}
static void dropEntryByName(std::vector<cloud::IdxEntry>& v, const std::string& name) {
  for (size_t i = 0; i < v.size(); i++)
    if (v[i].name == name) { v.erase(v.begin() + i); return; }
}

static bool readMeta(const std::string& name, trkr::Meta& m) {
  File f = LittleFS.open(webPath(name), "r");
  if (!f) return false;
  char buf[900];
  size_t n = f.readBytes(buf, sizeof(buf) - 1);
  f.close();
  buf[n] = 0;
  return trkr::metaFromText(buf, m);
}

// Fetches one website song, converts it and stores it as `a.name`.
// Returns 1 = stored, 0 = the device can't hold it (*why says why), -1 = network problem.
static int doDownload(const cloud::Action& a, cloud::IdxEntry& entry, const char** why) {
  {
    Http h;
    if (!h.open(String("/api/songs/") + a.id.c_str() + "?format=trk", true)) return -1;
    int rc = h.http.GET();
    if (rc < 0) h.note(rc);
    if (rc != 200) { h.http.end(); return -1; }
    File f = LittleFS.open(TMP_DOWN, "w");
    if (!f) { h.http.end(); return -1; }
    int n = h.http.writeToStream(&f);  // copes with chunked responses
    f.close();
    h.http.end();
    if (n < 0) { LittleFS.remove(TMP_DOWN); return -1; }
  }  // the secure connection is closed (and its memory freed) before converting
  trkr::Meta meta;
  trkr::Song* ws = new trkr::Song();
  File r = LittleFS.open(TMP_DOWN, "r");
  bool ok = r && trkr::load(r, *ws, why);
  if (r) r.close();
  LittleFS.remove(TMP_DOWN);
  if (!ok) { delete ws; return 0; }
  seq::Song* ds = new seq::Song();
  ok = trkr::toDevice(*ws, *ds, meta, why);
  delete ws;
  if (ok) ok = seq::songSaveCopy(a.name.c_str(), *ds);
  delete ds;
  if (!ok) { if (why && !*why) *why = "couldn't store it"; return 0; }
  char mt[900];
  trkr::metaToText(meta, mt, sizeof(mt));
  File w = LittleFS.open(webPath(a.name), "w");
  if (w) { w.print(mt); w.close(); }
  entry = {a.name, a.id, a.version, crc32File(cmsPath(a.name))};
  return 1;
}

// Sends a device song to the website: id empty = as a new song. Returns the
// HTTP status (200/201 stored, 409 the website moved on, -1 couldn't send).
static int doUpload(const std::string& name, const std::string& id, uint32_t version, bool deviceCopy, cloud::IdxEntry& entry,
                    const char** why) {
  {
    seq::Song* ds = new seq::Song();
    if (!seq::songLoadCopy(name.c_str(), *ds)) { delete ds; if (why) *why = "couldn't read it"; return -1; }
    trkr::Meta meta;
    bool have = readMeta(name, meta);
    if (have && deviceCopy) strncat(meta.title, " (device)", sizeof(meta.title) - strlen(meta.title) - 1);
    trkr::Song* ws = new trkr::Song();
    bool ok = trkr::toWeb(*ds, have ? &meta : nullptr, name.c_str(), *ws, why);
    delete ds;
    if (!ok) { delete ws; return -1; }
    File f = LittleFS.open(TMP_UP, "w");
    if (!f) { delete ws; return -1; }
    trkr::save(f, *ws);
    f.close();
    delete ws;
  }  // converted and written: now open the secure connection
  File f = LittleFS.open(TMP_UP, "r");
  if (!f) return -1;
  Http h;
  const bool isNew = id.empty();
  if (!h.open(isNew ? String("/api/songs") : String("/api/songs/") + id.c_str(), true)) { f.close(); return -1; }
  h.http.addHeader("Content-Type", "text/plain");
  if (!isNew) h.http.addHeader("If-Match", String(version));
  int rc = h.http.sendRequest(isNew ? "POST" : "PUT", &f, f.size());
  if (rc < 0) h.note(rc);
  String body = h.http.getString();
  h.http.end();
  f.close();
  LittleFS.remove(TMP_UP);
  if (rc == 200 || rc == 201) {
    entry = {name, kv(body, "id").c_str(), (uint32_t)kv(body, "version").toInt(), crc32File(cmsPath(name))};
    if (entry.id.empty()) entry.id = id;
  }
  return rc;
}

// A song with no notes in it (main starts with an empty "untitled") isn't worth sending.
static bool songIsEmpty(const std::string& name) {
  seq::Song* s = new seq::Song();
  bool empty = true;
  if (seq::songLoadCopy(name.c_str(), *s)) {
    for (const auto& tr : s->tracks)
      for (const auto& p : tr.patterns)
        if (!p.notes.empty()) empty = false;
  }  // a song that can't be read can't be sent either, so it counts as empty
  delete s;
  return empty;
}

static bool doSync() {
  nDown = nUp = 0;
  uint16_t nSkipped = 0, nFailed = 0;
  st = State::Syncing;
  if (!seq::storageAvailable(seq::Storage::Flash)) { setMsg("Can't open song storage"); return false; }
  setMsg("Checking songs...");

  // 1. What the website has.
  std::vector<cloud::RemoteSong> remote;
  {
    Http h;
    if (!h.open("/api/songs?since=1970-01-01&format=tsv", true)) { setMsg("Can't reach server"); return false; }
    int rc = h.http.GET();
    if (rc < 0) h.note(rc);
    if (rc == 401) {
      h.http.end();
      unlink();
      setMsg("Unlinked on the website - sign in again");
      return false;
    }
    if (rc != 200) {
      h.http.end();
      snprintf(msg, sizeof(msg), "Sync failed (HTTP %d)", rc);
      return false;
    }
    String body = h.http.getString();
    h.http.end();
    int pos = 0;
    while (pos < (int)body.length()) {
      int nl = body.indexOf('\n', pos);
      if (nl < 0) nl = body.length();
      String line = body.substring(pos, nl);
      pos = nl + 1;
      int a = line.indexOf('\t'), b = line.indexOf('\t', a + 1), c = line.indexOf('\t', b + 1);
      if (a < 0 || b < 0 || c < 0) continue;
      cloud::RemoteSong r;
      r.id = line.substring(0, a).c_str();
      r.version = (uint32_t)line.substring(a + 1, b).toInt();
      r.deleted = line.charAt(b + 1) == '1';
      r.title = line.substring(c + 1).c_str();
      remote.push_back(r);
    }
  }

  // 2. What the device has, and what was synced last time.
  std::vector<cloud::IdxEntry> idx = loadIndex();
  std::vector<cloud::LocalSong> local;
  for (const String& n : seq::songList(seq::Storage::Flash)) local.push_back({n.c_str(), crc32File(cmsPath(n.c_str()))});

  // New songs with nothing in them stay on the device.
  for (size_t i = 0; i < local.size();) {
    bool known = false;
    for (const auto& e : idx) known |= e.name == local[i].name;
    if (!known && songIsEmpty(local[i].name)) local.erase(local.begin() + i);
    else i++;
  }

  // 3. Carry out the plan.
  const std::vector<cloud::Action> todo = cloud::plan(remote, idx, local);
  std::vector<std::string> conflictNames;
  for (const auto& a : todo)
    if (a.act == cloud::Act::Detach && a.conflict) conflictNames.push_back(a.name);
  for (const auto& a : todo) {
    if (cancelReq) { saveIndex(idx); return false; }
    cloud::IdxEntry e;
    const char* why = nullptr;
    switch (a.act) {
      case cloud::Act::Download: {
        snprintf(msg, sizeof(msg), "Downloading %.20s...", a.name.c_str());
        int r = doDownload(a, e, &why);
        if (r == 1) { setEntry(idx, e); nDown++; }
        else if (r == 0) { setEntry(idx, {"", a.id, a.version, 0}); nSkipped++; }  // can't be held: don't retry until it changes
        else nFailed++;
        break;
      }
      case cloud::Act::Upload:
      case cloud::Act::UploadNew: {
        snprintf(msg, sizeof(msg), "Uploading %.22s...", a.name.c_str());
        bool dc = false;
        for (const auto& n : conflictNames) dc |= n == a.name;
        int rc = doUpload(a.name, a.act == cloud::Act::Upload ? a.id : std::string(), a.version, dc, e, &why);
        if (rc == 200 || rc == 201) { setEntry(idx, e); nUp++; }
        else if (rc == 409 && a.act == cloud::Act::Upload) {
          // The website moved on while we were syncing: keep ours as a new song online.
          dropEntryByName(idx, a.name);
          int r2 = doUpload(a.name, std::string(), 0, true, e, &why);
          if (r2 == 200 || r2 == 201) { setEntry(idx, e); nUp++; } else nFailed++;
        } else nFailed++;
        break;
      }
      case cloud::Act::DropLocal:
        LittleFS.remove(cmsPath(a.name));
        LittleFS.remove(webPath(a.name));
        dropEntryByName(idx, a.name);
        break;
      case cloud::Act::Detach:
        dropEntryByName(idx, a.name);  // the device copy stays; it is sent up as a new song below
        break;
      case cloud::Act::Tombstone:
        setEntry(idx, {"", a.id, a.version, 0});
        LittleFS.remove(webPath(a.name));
        break;
    }
    saveIndex(idx);  // after every step, so an interrupted sync loses nothing
  }
  saveIndex(idx);
  syncedAt = millis();
  if (nFailed) snprintf(msg, sizeof(msg), "Synced %u down, %u up, %u failed", nDown, nUp, nFailed);
  else if (nSkipped) snprintf(msg, sizeof(msg), "Synced %u down, %u up, %u too big", nDown, nUp, nSkipped);
  else if (!nDown && !nUp) setMsg("Everything is up to date");
  else snprintf(msg, sizeof(msg), "Synced: %u down, %u up", nDown, nUp);
  return nFailed == 0;
}

static void worker(void*) {
  bool ok = false;
  // Bluetooth holds ~60 KB even when switched off, so it is unloaded for the
  // job (the UI has already released the speaker and shrunk the screen).
  uint32_t before = esp_get_free_heap_size();
  midi::pauseBle();
  bleFreedKb = (uint16_t)((esp_get_free_heap_size() - before) / 1024);
  if (!hasWifi()) setMsg("Set up WiFi first");
  else if (wifiUp() && syncClock() && resolveHost()) {
    if (job == Job::Link) ok = doLink();
    else if (job == Job::Login) ok = doLogin();
    else if (!linked()) setMsg("Sign in first");
    else ok = doSync();
  }
  if (cancelReq) setMsg("Cancelled");
  memset(loginPass, 0, sizeof(loginPass));
  wifiDown();
  // Give the UI the freed RAM back first (it needs one big block for the
  // screen), and only then reload Bluetooth.
  fin = true;
  for (uint32_t t0 = millis(); !uiRestored && millis() - t0 < 3000;) delay(20);
  midi::resumeBle();
  st = ok ? State::Done : State::Failed;
  task = nullptr;
  vTaskDelete(nullptr);
}

// A job is requested here but only begins once the UI has freed memory: the
// worker needs a 12 KB stack for TLS, so it must be created AFTER that.
static void start(Job j) {
  if (task || pending) return;
  if (!cfgHost[0]) strcpy(cfgHost, DEFAULT_HOST);
  job = j;
  cancelReq = false;
  detailBuf[0] = 0;
  fin = false;
  uiRestored = false;
  if (j != Job::Login) memset(loginPass, 0, sizeof(loginPass));
  st = State::Connecting;
  setMsg("Making room...");
  pending = true;
}

void memoryReady() {
  if (!pending) return;
  pending = false;
  setMsg("Starting...");
  if (xTaskCreatePinnedToCore(worker, "cloud", 12288, nullptr, 1, &task, 0) != pdPASS) {
    task = nullptr;
    st = State::Failed;
    fin = true;
    memset(loginPass, 0, sizeof(loginPass));
    snprintf(msg, sizeof(msg), "No memory: %uK free, %uK block", (unsigned)(esp_get_free_heap_size() / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
  }
}

void startLink() { start(Job::Link); }
void startSync() { start(Job::Sync); }
void startLogin(const char* email, const char* password) {
  if (task || pending) return;
  strncpy(loginEmail, email, sizeof(loginEmail) - 1);
  strncpy(loginPass, password, sizeof(loginPass) - 1);
  start(Job::Login);
}

}  // namespace cloud
