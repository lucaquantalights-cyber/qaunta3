// config.h — pins and tunables for the Quanta Controller board (ESP32-WROOM-32E).
#pragma once

// ---- Board: "Quanta 4 Channel Driver_BK FIXED4" V1.0 (EasyEDA, updated 2026-10-16)
//   DC 15 V jack -> OKI-78SR 5 V -> AMS1117 3.3 V;  TC7660 makes -5 V for the LM2902 op-amps.
//   Bus 1 (IO21/IO22): U9 MCP4728A4 DAC (0x64), U5 ADS7828 monitor ADC (0x48), U16 DS3231 clock (0x68)
//   Bus 2 (IO16/IO17): U8 MCP4728A4 DAC (0x64)
//   IO26 SK6812 status LED, IO27 SW1 button, IO0 BOOT (programming header U15)
//   Four PJ-320A stereo jacks, OUT1..OUT8 = jack 1 tip, jack 1 ring, jack 2 tip, ... jack 4 ring.

// ---- I2C buses
#define PIN_SDA         21     // SDA1
#define PIN_SCL         22     // SCL1
#define I2C_HZ          400000
#define PIN_SDA2        16     // SDA2: second DAC alone, because both DACs are the same 0x64 part
#define PIN_SCL2        17     // SCL2

// The firmware searches 0x60-0x67 on both buses, so any MCP4728 address version works.
#define ADDR_DAC_A      0x64   // U9 MCP4728A4 on bus 1: OUT1-OUT4
#define ADDR_DAC_B      0x64   // U8 MCP4728A4 on bus 2: OUT5-OUT8
#define ADDR_ADC        0x48   // U5 ADS7828, A0 = A1 = GND
#define ADDR_ADC2       0x49   // only for older boards with two ADS1115s
#define ADDR_RTC        0x68   // U16 DS3231SN, CR2032 (BT1) on VBAT keeps time with the power off

// Board has an ADS7828: 1 = always use it. 0 = auto-detect ADS7828 / ADS1115 (older boards).
#define ADC_IS_ADS7828  1

// Which chip channel feeds each output, in case the PCB routes them in a different order.
// Index = OUT1..OUT8. DAC: 0-3 = U9 VOUTA-D, 4-7 = U8 VOUTA-D.  Monitor: ADS7828 CH0-CH7 (Monitor1-8).
// CHECK ON THE FIRST BOARD: Settings > Expert, turn on one output at a time and measure the jack.
#define DAC_MAP         {0, 1, 2, 3, 4, 5, 6, 7}
#define MON_MAP         {0, 1, 2, 3, 4, 5, 6, 7}

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
#define NTP_SERVER1     "pool.ntp.org"
#define NTP_SERVER2     "time.nist.gov"
#define NTP_SERVER3     "time.google.com"
#define NTP_INTERVAL_S  3600   // ask the internet for the time every hour while online
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

// ---- monitor divider: op-amp output -> 60.4k/10k -> ADS7828 (2.5 V internal ref, 2.2 uF on REFIN/OUT), BAT54 clamp
#define MON_DIVIDER     7.04f
#define ADC_REF_V       2.5f
