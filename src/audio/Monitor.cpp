#include "Monitor.h"

#include <M5Cardputer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace monitor {

static constexpr uint32_t SAMPLE_RATE = 22050;
static constexpr int BLOCK = 128;          // 5.8 ms per block; two queued = ~12 ms of latency
static constexpr int SPEAKER_CHANNEL = 0;
static constexpr int IDLE_BLOCKS = 200;    // after ~1.2 s of silence the stream pauses

struct Cmd {
  uint8_t type;  // 0 note on, 1 note off, 2 drum, 3 all off
  uint8_t note, vel, timbre, owner;
};

static synth::Core* core = nullptr;
static QueueHandle_t queue = nullptr;
static volatile bool enabled = true;
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
  }
}

static void audioTask(void*) {
  int cur = 0, idle = 0;
  for (;;) {
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
    core->render(bufs[cur], BLOCK);
    idle = (core->activeVoices() == 0 && core->activeDrums() == 0) ? idle + 1 : 0;
    while (M5Cardputer.Speaker.isPlaying(SPEAKER_CHANNEL) >= 2) vTaskDelay(1);
    M5Cardputer.Speaker.playRaw(bufs[cur], BLOCK, SAMPLE_RATE, false, 1, SPEAKER_CHANNEL, false);
    cur = (cur + 1) % 3;
  }
}

void begin() {
  core = new synth::Core((float)SAMPLE_RATE);
  queue = xQueueCreate(96, sizeof(Cmd));
  // Above the generator and network tasks (priority 1), pinned away from the main loop.
  xTaskCreatePinnedToCore(audioTask, "audio", 6144, nullptr, 3, nullptr, 0);
}

void setEnabled(bool on) {
  enabled = on;
  if (!on) send({3, 0, 0, 0, 0});
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

}  // namespace monitor
