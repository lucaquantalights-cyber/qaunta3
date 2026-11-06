// hal.h — drivers for the chips on the controller board.
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "core.h"

namespace hal {

// ======================================================================= MCP4728 quad DAC
// Two chips give 8 outputs. Production board: both on one bus, at 0x60 and 0x61 (factory address
// variants). Hand-built prototype with two stock breakouts (both 0x60): the second one sits on its own
// bus (PIN_SDA2/PIN_SCL2) and is found there automatically. The DAC keeps its output while the ESP32 resets (watchdog, update,
// crash), so the lights don't blink. Its EEPROM decides the power-up value: we keep that at 0 V,
// then the firmware ramps up to the scheduled level.
struct Mcp4728 {
  uint8_t addr;
  bool ok = false;
  TwoWire* bus = &Wire;
  explicit Mcp4728(uint8_t a, TwoWire* w = &Wire) : addr(a), bus(w) {}

  bool begin() {
    bus->beginTransmission(addr);
    ok = bus->endTransmission() == 0;
    if (!ok) return false;
    bus->beginTransmission(addr); bus->write(0x8F); bus->endTransmission();          // internal 2.048 V ref, all channels
    bus->beginTransmission(addr); bus->write(0xC0); bus->endTransmission();          // gain x1, all channels
    bus->beginTransmission(addr); bus->write(0xA0); bus->write(0x00); bus->endTransmission();  // powered up
    return true;
  }

  // Read the 4 output registers and 4 EEPROM values.
  bool read(uint16_t cur[4], uint16_t eep[4], bool eepIntRef[4]) {
    if (bus->requestFrom((int)addr, 24) != 24) return false;
    for (int c = 0; c < 4; c++) {
      uint8_t b[6]; for (int i = 0; i < 6; i++) b[i] = bus->read();
      cur[c] = ((b[1] & 0x0F) << 8) | b[2];
      eep[c] = ((b[4] & 0x0F) << 8) | b[5];
      eepIntRef[c] = b[4] & 0x80;
    }
    return true;
  }

  // Make sure the chip powers up at 0 V with the internal reference. Writes EEPROM only if needed
  // (normally once in the unit's life), because EEPROM has limited write cycles.
  bool ensurePowerUpZero() {
    uint16_t cur[4], eep[4]; bool ir[4];
    if (!read(cur, eep, ir)) return false;
    bool need = false;
    for (int c = 0; c < 4; c++) if (eep[c] != 0 || !ir[c]) need = true;
    if (!need) return true;
    // Happens once per chip (factory-fresh). Outputs briefly go to 0 V, then are restored.
    for (int c = 0; c < 4; c++) {
      bus->beginTransmission(addr);
      bus->write(0x58 | (c << 1));                      // single write DAC+EEPROM, channel c
      bus->write(0x80); bus->write(0x00);
      bus->endTransmission();
      delay(60);
    }
    write(cur);                                         // put the live outputs back where they were
    return true;
  }

  // Fast write: all 4 outputs, volatile registers only. Output updates immediately (LDAC tied low).
  bool write(const uint16_t code[4]) {
    bus->beginTransmission(addr);
    for (int c = 0; c < 4; c++) { bus->write((code[c] >> 8) & 0x0F); bus->write(code[c] & 0xFF); }
    return bus->endTransmission() == 0;
  }
};

// ======================================================================= output monitor ADC
// Quanta 4 Channel Driver board: one ADS7828 (0x48, 8 channels, 12 bit, 2.5 V internal reference).
// With ADC_IS_ADS7828 = 0 it also recognises older builds with two ADS1115 (0x48 = CH0-3, 0x49 = CH4-7),
// told apart by the ADS1115's Hi_thresh register, which reads 0x7FFF after power-up and is never written.
struct MonitorAdc {
  enum Kind : uint8_t { NONE, ADS7828, ADS1115 };
  Kind kind = NONE;
  bool ok = false;
  bool has2 = false;
  static bool rd16(uint8_t a, uint8_t reg, uint16_t& v) {
    Wire.beginTransmission(a); Wire.write(reg);
    if (Wire.endTransmission() != 0 || Wire.requestFrom((int)a, 2) != 2) return false;
    v = (Wire.read() << 8) | Wire.read(); return true;
  }
  static bool wr16(uint8_t a, uint8_t reg, uint16_t v) {
    Wire.beginTransmission(a); Wire.write(reg); Wire.write(v >> 8); Wire.write(v & 0xFF);
    return Wire.endTransmission() == 0;
  }
  bool begin() {
    kind = NONE; ok = false;
    Wire.beginTransmission(ADDR_ADC); if (Wire.endTransmission() != 0) return false;
#if ADC_IS_ADS7828
    kind = ADS7828; ok = true;
    readRaw7828(0); delay(10);                // turns the internal reference on; let the 2.2 uF cap charge
    readRaw7828(0);
    return true;
#endif
    uint16_t v = 0;
    if (rd16(ADDR_ADC, 0x03, v) && v == 0x7FFF) {
      Wire.beginTransmission(ADDR_ADC2); has2 = Wire.endTransmission() == 0;
      kind = ADS1115; ok = true;            // CH4-7 need the second chip (0x49); a bench test may have only one
    } else { kind = ADS7828; ok = true; }
    return ok;
  }
  static uint8_t sel7828(uint8_t ch) { static const uint8_t m[8] = {0, 4, 1, 5, 2, 6, 3, 7}; return m[ch & 7]; }
  // One conversion of chip channel CHn (0-7). Returns 0-4095, or -1.
  static int readRaw7828(uint8_t chipCh) {
    Wire.beginTransmission(ADDR_ADC);
    Wire.write(0x80 | (sel7828(chipCh) << 4) | 0x0C);     // single-ended, internal ref on, ADC on
    if (Wire.endTransmission() != 0) return -1;
    if (Wire.requestFrom((int)ADDR_ADC, 2) != 2) return -1;
    return ((Wire.read() & 0x0F) << 8) | Wire.read();
  }
  // Returns millivolts at the op-amp output of output ch (OUT1 = 0, after undoing the divider), or -1.
  int readAmpMv(uint8_t ch) {
    static const uint8_t monMap[8] = MON_MAP;
    if (kind == ADS7828) {
      int raw = readRaw7828(monMap[ch & 7]);
      if (raw < 0) return -1;
      return (int)(raw * ADC_REF_V / 4096.0f * MON_DIVIDER * 1000.0f + 0.5f);
    }
    if (kind == ADS1115) {
      if (ch >= 4 && !has2) return -1;
      uint8_t a = ch < 4 ? ADDR_ADC : ADDR_ADC2;
      // single-shot, AINx vs GND, +/-4.096 V, 860 SPS, comparator off
      uint16_t cfg = 0x8000 | ((4 + (ch & 3)) << 12) | (1 << 9) | (1 << 8) | (7 << 5) | 3;
      if (!wr16(a, 0x01, cfg)) return -1;
      uint16_t c = 0;
      for (int i = 0; i < 10; i++) { delayMicroseconds(400); if (rd16(a, 0x01, c) && (c & 0x8000)) break; }
      uint16_t raw;
      if (!rd16(a, 0x00, raw)) return -1;
      int32_t r = (int16_t)raw; if (r < 0) r = 0;          // clamp diode allows a few mV below 0
      return (int)(r * 0.125f * MON_DIVIDER + 0.5f);       // 0.125 mV per bit at +/-4.096 V
    }
    return -1;
  }
};

// ======================================================================= DS3231 RTC (kept in UTC)
// The CR2032 on VBAT keeps the oscillator running while the controller is unplugged. If the cell is
// dead (or missing) and the power goes off, the chip sets OSF ("oscillator stopped"), and read() then
// returns 0 so the firmware never trusts a wrong time. The DS3231 can't measure the cell voltage itself,
// so "lost time while unplugged" is the battery warning.
struct Ds3231 {
  uint8_t addr = ADDR_RTC;
  bool ok = false;
  bool lostPower = false;     // OSF was set: the clock stopped while unplugged -> replace the CR2032
  static bool rd8(uint8_t a, uint8_t reg, uint8_t& v) {
    Wire.beginTransmission(a); Wire.write(reg);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom((int)a, 1) != 1) return false;
    v = Wire.read(); return true;
  }
  static bool wr8(uint8_t a, uint8_t reg, uint8_t v) {
    Wire.beginTransmission(a); Wire.write(reg); Wire.write(v); return Wire.endTransmission() == 0;
  }
  static uint8_t bcd2(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
  static uint8_t tobcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

  bool begin() {
    Wire.beginTransmission(addr); ok = Wire.endTransmission() == 0;
    if (!ok) return false;
    // Control: oscillator on battery (EOSC = 0), no square wave (INT/SQW not connected), alarms off.
    uint8_t c;
    if (rd8(addr, 0x0E, c) && c != 0x1C) wr8(addr, 0x0E, 0x1C);
    // Status: remember OSF; turn the unused 32 kHz output off (saves coin-cell current).
    uint8_t st;
    if (rd8(addr, 0x0F, st)) {
      lostPower = st & 0x80;
      if (st & 0x08) wr8(addr, 0x0F, st & 0x80);       // keep OSF as is, clear EN32kHz and alarm flags
    }
    return true;
  }

  // Days from civil date (Howard Hinnant's algorithm), no timegm() needed.
  static int32_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int32_t)doe - 719468;
  }

  // Returns epoch seconds, or 0 if the clock has lost time (oscillator stopped / battery dead).
  uint32_t read() {
    if (!ok) return 0;
    Wire.beginTransmission(addr); Wire.write(0x0F);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom((int)addr, 1) != 1) return 0;
    if (Wire.read() & 0x80) { lostPower = true; return 0; }   // OSF: time is not trustworthy
    Wire.beginTransmission(addr); Wire.write(0x00);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom((int)addr, 7) != 7) return 0;
    uint8_t r[7]; for (int i = 0; i < 7; i++) r[i] = Wire.read();
    int sec = bcd2(r[0] & 0x7F), min = bcd2(r[1]), hr = bcd2(r[2] & 0x3F);
    int day = bcd2(r[4]), mon = bcd2(r[5] & 0x1F), yr = 2000 + bcd2(r[6]) + ((r[5] & 0x80) ? 100 : 0);
    if (mon < 1 || mon > 12 || day < 1 || day > 31 || hr > 23 || min > 59 || sec > 59) return 0;
    int64_t e = (int64_t)daysFromCivil(yr, mon, day) * 86400 + hr * 3600 + min * 60 + sec;
    return e > 1700000000 && e < 4000000000LL ? (uint32_t)e : 0;
  }

  bool write(uint32_t epoch) {
    if (!ok) return false;
    time_t t = epoch; struct tm u; gmtime_r(&t, &u);
    Wire.beginTransmission(addr); Wire.write(0x00);
    Wire.write(tobcd(u.tm_sec)); Wire.write(tobcd(u.tm_min)); Wire.write(tobcd(u.tm_hour));
    Wire.write(tobcd(u.tm_wday + 1)); Wire.write(tobcd(u.tm_mday));
    Wire.write(tobcd(u.tm_mon + 1)); Wire.write(tobcd((u.tm_year + 1900) % 100));
    if (Wire.endTransmission() != 0) return false;
    Wire.beginTransmission(addr); Wire.write(0x0F); Wire.write(0x00);   // clear OSF
    return Wire.endTransmission() == 0;   // lostPower stays set until the next start, so the warning is seen
  }

  // Chip temperature, °C x 4 (it sits inside the controller box). INT16_MIN if unavailable.
  int16_t tempQuarterC() {
    if (!ok) return INT16_MIN;
    Wire.beginTransmission(addr); Wire.write(0x11);
    if (Wire.endTransmission(false) != 0 || Wire.requestFrom((int)addr, 2) != 2) return INT16_MIN;
    int8_t msb = (int8_t)Wire.read(); uint8_t lsb = Wire.read();
    return (int16_t)(msb * 4 + (lsb >> 6));
  }
};

}  // namespace hal
