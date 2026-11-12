#include "Cloud.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <WiFi.h>
#include <string.h>

#include <vector>

#include "../song/SongStore.h"

// Root certificates that ship with ESP-IDF (the Mozilla bundle), so the
// device checks it is really talking to trkr.one.
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace cloud {

static const char* DEFAULT_HOST = "trkr.one";

static volatile State st = State::Idle;
static char msg[64] = "";
static char code[16] = "";
static char url[48] = "";
static char cfgSsid[33] = "", cfgPass[65] = "", cfgToken[64] = "", cfgEmail[64] = "", cfgHost[40] = "";
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

static void loadPrefs() {
  Preferences p;
  if (!p.begin("cloud", true)) return;
  p.getString("ssid", cfgSsid, sizeof(cfgSsid));
  p.getString("pass", cfgPass, sizeof(cfgPass));
  p.getString("token", cfgToken, sizeof(cfgToken));
  p.getString("email", cfgEmail, sizeof(cfgEmail));
  if (!p.getString("host", cfgHost, sizeof(cfgHost))) strcpy(cfgHost, DEFAULT_HOST);
  p.end();
  if (!cfgHost[0]) strcpy(cfgHost, DEFAULT_HOST);
}

static void savePref(const char* key, const char* val) {
  Preferences p;
  if (!p.begin("cloud", false)) return;
  p.putString(key, val);
  p.end();
}

void begin() { loadPrefs(); }
State state() { return st; }
const char* message() { return msg; }
const char* linkCode() { return code; }
const char* linkUrl() { return url; }
bool hasWifi() { return cfgSsid[0] != 0; }
const char* ssid() { return cfgSsid; }
bool linked() { return cfgToken[0] != 0; }
const char* email() { return cfgEmail; }
const char* host() { return cfgHost; }
bool busy() { return task != nullptr; }
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
  for (int i = 0; i < n; i++) addNet(nets, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
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
  savePref("ssid", cfgSsid);
  savePref("pass", cfgPass);
}

void unlink() {
  cfgToken[0] = 0;
  cfgEmail[0] = 0;
  savePref("token", "");
  savePref("email", "");
}

// ── HTTP helpers ─────────────────────────────────────────────────────────────
static bool wifiUp() {
  if (WiFi.status() == WL_CONNECTED) return true;
  st = State::Connecting;
  setMsg("Joining WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfgSsid, cfgPass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (cancelReq) return false;
    if (millis() - t0 > 20000) {
      setMsg("WiFi: couldn't connect");
      return false;
    }
    delay(200);
  }
  return true;
}

static void wifiDown() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

struct Http {
  NetworkClientSecure tls;
  HTTPClient http;
  Http() {
    tls.setCACertBundle(x509_crt_bundle_start, x509_crt_bundle_end - x509_crt_bundle_start);
    http.setReuse(false);
    http.setTimeout(15000);
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
    String body = h.http.getString();
    h.http.end();
    if (rc == 200) {
      strncpy(cfgToken, kv(body, "token").c_str(), sizeof(cfgToken) - 1);
      strncpy(cfgEmail, kv(body, "email").c_str(), sizeof(cfgEmail) - 1);
      savePref("token", cfgToken);
      savePref("email", cfgEmail);
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
  body = "";
  String resp = h.http.getString();
  h.http.end();
  if (rc == 200) {
    strncpy(cfgToken, kv(resp, "token").c_str(), sizeof(cfgToken) - 1);
    strncpy(cfgEmail, kv(resp, "email").c_str(), sizeof(cfgEmail) - 1);
    savePref("token", cfgToken);
    savePref("email", cfgEmail);
    snprintf(msg, sizeof(msg), "Signed in as %s", cfgEmail);
    return true;
  }
  if (rc == 401) setMsg("Wrong email or password");
  else if (rc == 429) setMsg("Too many tries - wait 15 min");
  else snprintf(msg, sizeof(msg), "Sign-in failed (HTTP %d)", rc);
  return false;
}

// ── sync ─────────────────────────────────────────────────────────────────────
struct Remote {
  String id;
  uint32_t version;
  bool deleted;
};

static bool download(const String& id, store::Entry& e) {
  Http h;
  if (!h.open("/api/songs/" + id + "?format=trk", true)) return false;
  int rc = h.http.GET();
  if (rc != 200) { h.http.end(); return false; }
  // writeToStream copes with chunked responses; save to a temp file first,
  // then the store validates it before replacing anything.
  const char* tmp = "/songs/download.tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) { h.http.end(); return false; }
  int n = h.http.writeToStream(&f);
  f.close();
  h.http.end();
  if (n < 0) { LittleFS.remove(tmp); return false; }
  File r = LittleFS.open(tmp, "r");
  bool ok = r && store::saveStream(e, r, true);
  r.close();
  LittleFS.remove(tmp);
  return ok;
}

static int upload(store::Entry& e) {
  File f = LittleFS.open(store::path(e.file), "r");
  if (!f) return -1;
  Http h;
  bool isNew = !e.cloudId[0];
  if (!h.open(isNew ? String("/api/songs") : String("/api/songs/") + e.cloudId, true)) return -1;
  h.http.addHeader("Content-Type", "text/plain");
  if (!isNew) h.http.addHeader("If-Match", String(e.version));
  int rc = h.http.sendRequest(isNew ? "POST" : "PUT", &f, f.size());
  String body = h.http.getString();
  h.http.end();
  f.close();
  if (rc == 200 || rc == 201) {
    strncpy(e.cloudId, kv(body, "id").c_str(), sizeof(e.cloudId) - 1);
    e.version = kv(body, "version").toInt();
    e.dirty = false;
    store::update(e);
  }
  return rc;
}

static bool doSync() {
  nDown = nUp = 0;
  st = State::Syncing;
  setMsg("Checking songs...");
  std::vector<Remote> remote;
  {
    Http h;
    if (!h.open("/api/songs?since=1970-01-01&format=tsv", true)) { setMsg("Can't reach server"); return false; }
    int rc = h.http.GET();
    if (rc == 401) {
      h.http.end();
      unlink();
      setMsg("Unlinked on the website - link again");
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
      remote.push_back({line.substring(0, a), (uint32_t)line.substring(a + 1, b).toInt(), line.charAt(b + 1) == '1'});
    }
  }

  // 1. Bring the device up to date with the server.
  for (const auto& r : remote) {
    if (cancelReq) return false;
    store::Entry local;
    bool have = store::find(r.id.c_str(), local);
    if (r.deleted) {
      if (!have) continue;
      if (local.dirty) {  // deleted online but changed here: keep it as a device-only song
        local.cloudId[0] = 0;
        local.version = 0;
        store::update(local);
      } else {
        store::remove(local.file);
      }
      continue;
    }
    if (have && local.version >= r.version) continue;
    if (have && local.dirty) {
      // Changed in both places: keep the device copy as a new song.
      local.cloudId[0] = 0;
      local.version = 0;
      strncat(local.name, " (device)", sizeof(local.name) - strlen(local.name) - 1);
      store::update(local);
      have = false;
    }
    store::Entry e = have ? local : store::Entry();
    strncpy(e.cloudId, r.id.c_str(), sizeof(e.cloudId) - 1);
    e.version = r.version;
    snprintf(msg, sizeof(msg), "Downloading %u...", nDown + 1);
    if (download(r.id, e)) nDown++;
  }

  // 2. Send songs changed on the device.
  for (auto& e : store::list()) {
    if (cancelReq) return false;
    if (!e.dirty) continue;
    snprintf(msg, sizeof(msg), "Uploading %s", e.name);
    int rc = upload(e);
    if (rc == 200 || rc == 201) nUp++;
    else if (rc == 409) {
      // Server moved on since our copy: keep ours as a new song online.
      e.cloudId[0] = 0;
      e.version = 0;
      if (upload(e) == 201) nUp++;
    }
  }
  syncedAt = millis();
  snprintf(msg, sizeof(msg), "Synced: %u down, %u up", nDown, nUp);
  return true;
}

static void worker(void*) {
  bool ok = false;
  if (!hasWifi()) setMsg("Set up WiFi first");
  else if (wifiUp()) {
    if (job == Job::Link) ok = doLink();
    else if (job == Job::Login) ok = doLogin();
    else if (!linked()) setMsg("Link your account first");
    else ok = doSync();
  }
  if (cancelReq) setMsg("Cancelled");
  memset(loginPass, 0, sizeof(loginPass));
  wifiDown();
  st = ok ? State::Done : State::Failed;
  task = nullptr;
  vTaskDelete(nullptr);
}

static void start(Job j) {
  if (task) return;
  job = j;
  cancelReq = false;
  if (j != Job::Login) memset(loginPass, 0, sizeof(loginPass));
  // TLS needs a deep stack.
  st = State::Connecting;
  setMsg("Starting...");
  if (xTaskCreatePinnedToCore(worker, "cloud", 12288, nullptr, 1, &task, 0) != pdPASS) {
    task = nullptr;
    st = State::Failed;
    memset(loginPass, 0, sizeof(loginPass));
    snprintf(msg, sizeof(msg), "No memory: %uK free, %uK block", (unsigned)(esp_get_free_heap_size() / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
  }
}

void startLink() { start(Job::Link); }
void startSync() { start(Job::Sync); }
void startLogin(const char* email, const char* password) {
  if (task) return;
  strncpy(loginEmail, email, sizeof(loginEmail) - 1);
  strncpy(loginPass, password, sizeof(loginPass) - 1);
  start(Job::Login);
}

}  // namespace cloud
