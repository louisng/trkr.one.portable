#pragma once
// trkr.one account link and song sync over WiFi.
//
// Linking works like signing in to a TV: the device shows a short code, you
// type it at trkr.one/link while signed in, and the device receives a token.
// Network work runs on a background task; the UI polls state().
#include <stdint.h>

#include "ScanList.h"

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
const char* detail();         // second status line: what was seen and what each try ended with
int bleFreedLastKb();         // RAM (KB) freed by unloading Bluetooth for the last job
const char* message();        // human-readable status of the last/current job
const char* linkCode();       // while Linking: the code to type at /link
const char* linkUrl();        // while Linking: where to type it

bool hasWifi();               // credentials saved
const char* ssid();
int passLen();                // length of the saved password (to check it was typed right)
void setWifi(const char* ssid, const char* pass);

// Scan for networks (runs in the background; poll scanning()).
void startScan();
bool scanning();              // true while the scan runs; false once results are ready
bool scanFailed();            // the last scan could not run
int scanCount();
const Net& scanNet(int i);    // strongest first, one per name, hidden networks left out
void endScan();               // turn the radio back off
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
// The UI frees RAM (screen colour, speaker) around a job: the job waits for
// memoryReady() before it starts and for restored() before it reloads Bluetooth.
bool needsMemory();           // a job is waiting for the UI to free RAM
void memoryReady();           // the UI has freed it: the job may now start
bool finishing();             // job done: the UI should now restore what it freed
void restored();

uint32_t lastSyncMs();        // millis() of the last good sync, 0 = never
uint16_t lastDownloaded();
uint16_t lastUploaded();

}  // namespace cloud
