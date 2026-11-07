#pragma once
// trkr.one account link and song sync over WiFi.
//
// Linking works like signing in to a TV: the device shows a short code, you
// type it at trkr.one/link while signed in, and the device receives a token.
// Network work runs on a background task; the UI polls state().
#include <stdint.h>

namespace cloud {

enum class State : uint8_t {
  Idle,
  Connecting,   // joining WiFi
  Linking,      // showing a code, waiting for approval
  Syncing,
  Done,         // last job finished OK (see message())
  Failed,       // last job failed (see message())
};

void begin();
State state();
const char* message();        // human-readable status of the last/current job
const char* linkCode();       // while Linking: the code to type at /link
const char* linkUrl();        // while Linking: where to type it

bool hasWifi();               // credentials saved
const char* ssid();
void setWifi(const char* ssid, const char* pass);
bool linked();
const char* email();
const char* host();
void unlink();

bool busy();
void startLink();             // background: WiFi → code → wait for approval
// background: WiFi → sign in with email + password. Only the resulting
// token is kept; the password is wiped as soon as the request is sent.
void startLogin(const char* email, const char* password);
void startSync();             // background: WiFi → two-way song sync
void cancel();

uint32_t lastSyncMs();        // millis() of the last good sync, 0 = never
uint16_t lastDownloaded();
uint16_t lastUploaded();

}  // namespace cloud
