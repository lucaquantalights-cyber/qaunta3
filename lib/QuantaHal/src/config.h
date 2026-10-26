// config.h — pins and tunables for the Quanta Controller board (ESP32-WROOM-32E).
#pragma once

// ---- I2C bus: DACs, ADC, RTC
#define PIN_SDA         21
#define PIN_SCL         22
#define I2C_HZ          400000
// second bus, only used by a hand-built prototype whose second MCP4728 breakout is also at 0x60
#define PIN_SDA2        16
#define PIN_SCL2        17

#define ADDR_DAC_A      0x60   // MCP4728A0: jack 1 tip, jack 1 ring, jack 2 tip, jack 2 ring
#define ADDR_DAC_B      0x61   // MCP4728A1: jacks 3 and 4
#define ADDR_DAC_ALT    0x64   // MCP4728A4: some Adafruit breakouts ship with this address
#define ADDR_ADC        0x48   // ADS1115 #1 (CH0-3) on the production board, or one ADS7828 (hand-built)
#define ADDR_ADC2       0x49   // ADS1115 #2 (CH4-7), ADDR pin to VDD
#define ADDR_RTC        0x68   // DS3231 with CR2032 backup

// ---- bench prototype: used only when the MCP4728 DAC chips are not found.
// Each pin drives an off-the-shelf "PWM to 0-10 V" module (LM358 type, 1-3 kHz input).
// Order: OUT1 blue, OUT1 white, OUT2 blue, OUT2 white, ... (avoids strapping and PSRAM pins)
#define PWM_PINS        {13, 14, 18, 19, 23, 25, 32, 33}
#define PWM_FREQ        1000
#define PWM_BITS        12

// ---- front panel
#define PIN_BUTTON      27     // to GND, internal pull-up
#define PIN_STATUS_LED  26     // one SK6812/WS2812 pixel

// ---- timing
#define CONTROL_MS      50     // output update period
#define MONITOR_MS      1000   // read output monitors
#define HEARTBEAT_S     300    // save "still powered at" time this often (outage log)
#define RTC_RESYNC_S    21600  // re-read the RTC every 6 h to correct ESP32 clock drift
#define OTA_CONFIRM_MS  60000  // new firmware must run this long before it's marked good
#define WDT_TIMEOUT_S   30     // long enough for a slow update server to answer

// ---- fleet updates: every controller reads the same manifest in the quanta/ folder.
// Put your server's quanta folder (or its firmware.json) here, e.g.
//   "https://your-project.web.app/quanta/firmware.json"
// It can also be set or changed on the Settings page.
#define OTA_DEFAULT_URL "https://aiesdoser.web.app/quanta/"
#define OTA_CHECK_HOURS 6      // automatic check interval while on home Wi-Fi
#define OTA_FIRST_CHECK_S 120  // first automatic check this long after joining Wi-Fi

// ---- Wi-Fi / GUI
#define MDNS_NAME       "quanta"             // http://quanta.local
#define AP_PASSWORD     ""                   // setup hotspot password: "" = open, like the reef doser (or 8+ chars)
#define STA_TIMEOUT_MS  20000
#define AP_AFTER_BUTTON_MIN 15               // long-press opens the hotspot for this long
#define PROV_HOLD_S     120                  // after Wi-Fi setup connects, keep the hotspot this long so the address can be written down
#define PROV_FAIL_S     45                   // setup page reports failure after this many seconds

// ---- monitor divider: op-amp output -> 60.4k/10k -> ADC (2.5 V internal ref), BAT85 clamp to GND
#define MON_DIVIDER     7.04f
#define ADC_REF_V       2.5f
