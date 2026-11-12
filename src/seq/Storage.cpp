// SD card access for song files. The card is mounted the first time it's
// needed; with no card, mounting is retried at most every few seconds so
// screens that ask often don't stall.
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>

#include "Song.h"

namespace seq {

static int8_t state = -1;  // -1 never tried, 0 no card, 1 mounted
static uint32_t lastTry = 0;
static bool spiStarted = false;

fs::FS* sdFs() {
  if (state == 1) {
    if (SD.cardType() != CARD_NONE) return &SD;
    SD.end();  // card was removed
    state = 0;
  }
  if (state == 0 && millis() - lastTry < 3000) return nullptr;
  lastTry = millis();

  const int sclk = M5.getPin(m5::pin_name_t::sd_spi_sclk);
  const int miso = M5.getPin(m5::pin_name_t::sd_spi_miso);
  const int mosi = M5.getPin(m5::pin_name_t::sd_spi_mosi);
  const int cs = M5.getPin(m5::pin_name_t::sd_spi_cs);
  if (sclk < 0 || miso < 0 || mosi < 0 || cs < 0) {
    state = 0;
    return nullptr;
  }
  // The display has its own SPI bus, so the SD card gets the default one.
  if (!spiStarted) {
    SPI.begin(sclk, miso, mosi, cs);
    spiStarted = true;
  }
  state = (SD.begin(cs, SPI, 20000000) && SD.cardType() != CARD_NONE) ? 1 : 0;
  return state == 1 ? &SD : nullptr;
}

}  // namespace seq
