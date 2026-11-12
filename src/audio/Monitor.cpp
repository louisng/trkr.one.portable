#include "Monitor.h"

#include <M5Cardputer.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace monitor {

static constexpr uint32_t SAMPLE_RATE = 22050;
static constexpr int BLOCK = 128;          // 5.8 ms per block; two queued = ~12 ms of latency
static constexpr int SPEAKER_CHANNEL = 0;
static constexpr int IDLE_BLOCKS = 200;    // after ~1.2 s of silence the stream pauses

struct Cmd {
  uint8_t type;  // 0 note on, 1 note off, 2 drum, 3 all off, 4 test beep
  uint8_t note, vel, timbre, owner;
};

// For the Setup screen, so a silent speaker can be diagnosed without a computer.
static volatile uint8_t state = 0;            // 0 not started, 1 streaming, 2 no speaker
static volatile uint32_t blocksPlayed = 0, blocksRefused = 0;
static int beepBlocksLeft = 0;

static synth::Core* core = nullptr;
static QueueHandle_t queue = nullptr;
static volatile bool enabled = true;
static volatile bool paused = false;    // asked to release the speaker
static volatile bool released = false;  // the audio task has released it
static int16_t bufs[3][BLOCK];   // one playing, one queued, one being filled

static void send(const Cmd& c) {
  if (queue) xQueueSend(queue, &c, 0);  // never wait: dropping a note beats stalling MIDI
}

static void apply(const Cmd& c) {
  switch (c.type) {
    case 0: core->noteOn(c.note, c.vel, c.timbre, c.owner); break;
    case 1: core->noteOff(c.note, c.owner); break;
    case 2: core->drum(c.note, c.vel); break;
    case 3: core->allOff(); break;
    case 4:  // test beep: a kick and a short electric-piano note
      core->drum(36, 110);
      core->noteOn(69, 100, synth::TB_EP, 9);
      beepBlocksLeft = 45;  // ~0.25 s
      break;
  }
}

static void audioTask(void*) {
  int cur = 0, idle = 0;
  for (;;) {
    // Always give other tasks a turn: if the speaker ever stops taking data this
    // loop must not spin, or the watchdog resets the whole device.
    vTaskDelay(1);
    if (paused) {
      if (!released) {
        core->allOff();
        M5Cardputer.Speaker.end();
        released = true;
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (released) {  // resume: restart the speaker, start from silence
      if (M5Cardputer.Speaker.begin()) {
        core->allOff();
        released = false;
        idle = 0;
      } else {
        vTaskDelay(pdMS_TO_TICKS(100));
        continue;
      }
    }
    Cmd c;
    bool silent = core->activeVoices() == 0 && core->activeDrums() == 0;
    // Nothing sounding for a while: sleep until the next command arrives.
    if (silent && idle >= IDLE_BLOCKS) {
      if (xQueueReceive(queue, &c, pdMS_TO_TICKS(100)) == pdTRUE) {
        apply(c);
        idle = 0;
      } else {
        continue;
      }
    }
    while (xQueueReceive(queue, &c, 0) == pdTRUE) {
      if (enabled || c.type == 3) apply(c);
      idle = 0;
    }
    if (!enabled) {
      core->allOff();
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (beepBlocksLeft > 0 && --beepBlocksLeft == 0) core->noteOff(69, 9);
    core->render(bufs[cur], BLOCK);
    idle = (core->activeVoices() == 0 && core->activeDrums() == 0) ? idle + 1 : 0;
    int waited = 0;
    while (M5Cardputer.Speaker.isPlaying(SPEAKER_CHANNEL) >= 2) {
      vTaskDelay(1);
      if (++waited > 50) break;   // the speaker has stalled: carry on rather than hang
    }
    if (M5Cardputer.Speaker.playRaw(bufs[cur], BLOCK, SAMPLE_RATE, false, 1, SPEAKER_CHANNEL, false)) {
      cur = (cur + 1) % 3;
      blocksPlayed++;
    } else {
      blocksRefused++;
      vTaskDelay(pdMS_TO_TICKS(10));   // refused (not ready): try again shortly
    }
  }
}

void begin() {
#ifdef CMC_NO_AUDIO
  return;   // diagnostic build: no speaker at all
#endif
  // This M5 library starts the speaker the first time something plays, so it is NOT
  // running yet here: start it explicitly. (An earlier version checked isRunning()
  // instead, always concluded "no speaker" and silently disabled all sound.)
  if (!M5Cardputer.Speaker.isEnabled() || !M5Cardputer.Speaker.begin()) {
    state = 2;
    return;
  }
  state = 1;
  core = new synth::Core((float)SAMPLE_RATE);
  queue = xQueueCreate(96, sizeof(Cmd));
  // Above the generator and network tasks (priority 1), pinned away from the main loop.
  xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 3, nullptr, 0);
}

void setEnabled(bool on) {
  enabled = on;
  if (!on) send({3, 0, 0, 0, 0});
}

void pause() {
  if (state != 1 || paused) return;
  paused = true;
  for (int i = 0; i < 100 && !released; i++) vTaskDelay(pdMS_TO_TICKS(10));  // wait for the task to let go
}

void resume() {
  if (state != 1 || !paused) return;
  paused = false;
  for (int i = 0; i < 100 && released; i++) vTaskDelay(pdMS_TO_TICKS(10));  // and to restart it
}

void setVolume(uint8_t v) { M5Cardputer.Speaker.setVolume(v); }

uint8_t timbreFor(const char* voice, uint8_t trackType) { return synth::timbreForVoice(voice, trackType); }

void noteOn(uint8_t note, uint8_t vel, uint8_t timbre, uint8_t owner) {
  if (enabled) send({0, note, vel, timbre, owner});
}
void noteOff(uint8_t note, uint8_t owner) { send({1, note, 0, 0, owner}); }
void drum(uint8_t gmNote, uint8_t vel) {
  if (enabled) send({2, gmNote, vel, 0, 0});
}
void allOff() { send({3, 0, 0, 0, 0}); }
void beep() { if (enabled) send({4, 0, 0, 0, 0}); }

const char* status() {
  static char buf[32];
  if (state == 1) snprintf(buf, sizeof(buf), "synth ok %lu/%lu", (unsigned long)blocksPlayed, (unsigned long)blocksRefused);
  else if (state == 2) snprintf(buf, sizeof(buf), "NO SPEAKER");
  else snprintf(buf, sizeof(buf), "synth off");
  return buf;
}

}  // namespace monitor
