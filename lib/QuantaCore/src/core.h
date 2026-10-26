// core.h — hardware-independent logic for the Quanta Controller.
// One box, 4 x 3.5 mm TRS jacks (outputs). Each output drives one light or a piggybacked chain:
//   Altair 100 / 210 : tip = blue channel, ring = white channel
//   Atlas, Helix TS / RCD / MB : one channel on the tip, ring held at 0 V
//   3rd-party equipment: the user decides what tip and ring do
//   Sleeve = ground. All dimming is 0-10 V.
// No Arduino includes, so this is unit-tested on a PC (test/test_core.cpp).
#pragma once
#include <stdint.h>
#include <string.h>

namespace qc {

// ------------------------------------------------------------------ sizes
constexpr uint8_t  JACKS        = 4;
constexpr uint8_t  CHANNELS     = JACKS * 2;   // ch = jack*2 + (0 tip, 1 ring)
constexpr uint8_t  MAX_GROUPS   = 8;            // = CHANNELS, so expert mode can give every channel its own curve
constexpr uint8_t  MAX_POINTS   = 16;
constexpr uint8_t  MAX_PHOTO    = 12;           // three picture modes for each of up to four aquariums
constexpr uint8_t  MAX_AQ       = 4;            // aquariums (guided setups) sharing the four outputs
constexpr uint8_t  NAME_LEN     = 20;
constexpr uint8_t  GNAME_LEN    = 56;           // group names carry the aquarium: "Ultimate SPS REEF: OUT 4 Meso Blue"
constexpr uint16_t FOLLOW       = 0xFFFF;       // a picture/manual level that means "keep following the schedule"
constexpr uint8_t  NO_AQ        = 0xFF;
constexpr uint16_t LEVEL_MAX    = 1000;        // levels are permille
constexpr uint16_t DAC_MAX      = 4095;
constexpr uint16_t NO_CLOCK_LEVEL = 250;       // brand-new box with no clock: 25 % so the user sees it works
constexpr uint32_t DAY          = 86400;

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
inline void copyName(char *dst, const char *src) {
  if (!src) src = "";
  strncpy(dst, src, NAME_LEN - 1); dst[NAME_LEN - 1] = 0;
}
inline void copyNameN(char *dst, const char *src, size_t n) {
  if (!src) src = "";
  strncpy(dst, src, n - 1); dst[n - 1] = 0;
}

// ------------------------------------------------------------------ fixtures
enum Fixture : uint8_t {
  FX_NONE = 0, FX_ALTAIR_100, FX_ALTAIR_210, FX_ATLAS, FX_HELIX_TS, FX_HELIX_RCD, FX_HELIX_MB, FX_OTHER, FX_COUNT
};
inline bool twoChannel(uint8_t fx) { return fx == FX_ALTAIR_100 || fx == FX_ALTAIR_210; }
inline bool isHelix(uint8_t fx)    { return fx == FX_HELIX_TS || fx == FX_HELIX_RCD || fx == FX_HELIX_MB; }
inline bool isQuanta(uint8_t fx)   { return fx != FX_NONE && fx != FX_OTHER && fx < FX_COUNT; }  // channels & watts locked

// Ids used in the JSON API / GUI.
inline const char *fixtureId(uint8_t fx) {
  static const char *ids[FX_COUNT] = {"none", "altair_100", "altair_210", "atlas", "helix_ts", "helix_rcd", "helix_mb", "other"};
  return fx < FX_COUNT ? ids[fx] : "none";
}
inline uint8_t fixtureFromId(const char *s) {
  if (!s) return FX_NONE;
  for (uint8_t i = 0; i < FX_COUNT; i++) if (!strcmp(s, fixtureId(i))) return i;
  return FX_NONE;
}

// ------------------------------------------------------------------ config
struct Point   { uint16_t min; uint16_t lvl; };             // minute of day, permille
// aq = the aquarium this guided group belongs to (NO_AQ in expert mode); inten = its own intensity
// (inside lights, Meso Blue bars, an output with its own setting; 0 = the aquarium's); role 0 blue, 1 white.
struct Group   { char name[GNAME_LEN]; uint8_t color; uint8_t n; uint8_t aq; uint8_t inten; uint8_t role; Point p[MAX_POINTS]; };
struct Jack    { uint8_t fixture; uint8_t count; char name[NAME_LEN]; uint16_t watts[2]; uint8_t len; };  // watts per light per channel; len = Helix length index
struct Channel { int8_t group; uint8_t trim; uint8_t cal; };  // group -1 = unassigned; trim 0..100 %; cal 80..120 %
struct Photo   { char name[NAME_LEN]; uint16_t minutes; uint8_t aq; uint16_t lvl[MAX_GROUPS]; };   // lvl FOLLOW = keep the schedule

// What the setup assistant was told (Luca's "Guided Set Up Mode"), one per aquarium.
struct Setup {
  uint8_t kind;          // SetupKind
  char    name[32];      // the aquarium's name
  uint8_t model;         // Altair Only: FX_ALTAIR_100 / FX_ALTAIR_210
  uint8_t qty;           // Altair Only: 1..6
  bool    par;           // Uniform PAR Mode (3..6 Altairs or Atlas only)
  uint8_t out, inn;      // PAR: outside / inside lights (out + inn = qty or atlas)
  uint8_t mb, rcd, ts;   // Helix AIO: bars of each spectrum, 0..6
  uint8_t len;           // Helix length index (HELIX_INCH)
  uint8_t supp;          // Helix Supplemental: FX_HELIX_MB / _RCD / _TS
  uint8_t suppQty;       // Helix Supplemental: 1..6
  uint8_t atlas, amb;    // Atlas & Helix: Atlas lights 1..6, Meso Blue bars 0..6
};
// One output's own times and intensity, set from the Now page (Edit).
struct OutAdj { uint8_t on; uint16_t onMin, offMin; uint8_t inten[2]; };
// One aquarium: its setup, schedule settings, acclimation and the outputs it uses (in plan order).
struct Aq {
  Setup    setup;
  uint8_t  tank, spectrum;
  uint16_t offMin;
  uint8_t  intensity, intenIn, intenMB;
  bool     acclOn; uint8_t acclStartPct; uint16_t acclDays; uint32_t acclStart;
  uint8_t  nOut; uint8_t out[JACKS];
  char     outName[JACKS][NAME_LEN];       // "" = the plan's name
  OutAdj   adj[JACKS];
};

struct Config {
  uint8_t  nGroups;
  Group    group[MAX_GROUPS];
  Jack     jack[JACKS];
  Channel  ch[CHANNELS];
  uint8_t  nPhoto;
  Photo    photo[MAX_PHOTO];
  uint8_t  intensity;      // schedule scale, 10..100 %
  uint8_t  floorPct;       // lowest non-zero level (Mean Well drivers flicker when very low)
  uint16_t rampSec;        // soft start after a power cut
  uint16_t fadeSec;        // how long a full-range change takes (mode switches, slider moves)
  bool     acclOn;
  uint8_t  acclStartPct;   // acclimation starts at this % of the schedule...
  uint16_t acclDays;       // ...and reaches 100 % after this many days
  uint32_t acclStart;      // epoch seconds (UTC)
  char     tz[48];         // POSIX TZ string
  bool     expert;
  bool     setupDone;
  uint16_t costMils;       // electricity price, 1/1000 $ per kWh (150 = $0.150)
  uint8_t  tank;           // guided: 0 Softies & LPS, 1 Mixed, 2 SPS
  uint8_t  spectrum;       // guided: 0 Shallow Reef, 1 Lagoon Reef, 2 Grow and Pop, 3 Blue, 4 Ultra Blue
  uint16_t offMin;         // guided: lights-off, minute of day (lights-on follows from the tank type)
  uint8_t  intenIn;        // guided, Uniform PAR Mode: intensity of the inside lights, 10..100 %
  using Setup = ::qc::Setup;
  Setup    setup;          // guided: the setup of the aquarium being looked at (sel) - the pages edit these
  uint8_t  intenMB;        // guided, Atlas & Helix: Meso Blue bars' own intensity
  uint8_t  nAq, sel;       // guided: aquariums and which one the pages are on
  Aq       aq[MAX_AQ];     // tank..intenMB, acclimation and setup above mirror aq[sel]
};

// ------------------------------------------------------------------ product line
// Helix lengths (inches) and wattage per bar.
constexpr uint8_t  HELIX_LENS = 5;
constexpr uint8_t  HELIX_INCH[HELIX_LENS]  = {23, 35, 47, 59, 71};
constexpr uint16_t HELIX_WATTS[HELIX_LENS] = {38, 57, 74, 92, 111};
// Watts per light per channel (side 0 = tip, 1 = ring).
inline uint16_t fixtureWatts(uint8_t fx, uint8_t side, uint8_t len) {
  switch (fx) {
    case FX_ALTAIR_210: return side == 0 ? 150 : 60;
    case FX_ALTAIR_100: return side == 0 ? 70 : 30;
    case FX_ATLAS:      return side == 0 ? 240 : 0;
    case FX_HELIX_TS: case FX_HELIX_RCD: case FX_HELIX_MB:
                        return side == 0 ? HELIX_WATTS[len < HELIX_LENS ? len : 0] : 0;
    default:            return 0;
  }
}

// Guided groups are built from the setup (see guidedPlan()). Colours are fixed: Blue and White are the
// Altairs' (and the Helix/Atlas channels that follow them); inside lights get lighter shades.
enum GuidedGroup : uint8_t { GG_BLUE = 0, GG_WHITE = 1 };
enum Color : uint8_t { COL_BLUE = 0, COL_WHITE, COL_YELLOW, COL_ORANGE, COL_CYAN, COL_INDIGO,
                       COL_GREEN, COL_PINK, COL_PURPLE, COL_RED, COL_LT_BLUE, COL_LT_WHITE, COL_COUNT };

// Default channel -> group when a product is placed by hand: blue-type channels follow Blue, white-type White.
inline int8_t defaultGroup(uint8_t fx, uint8_t side) {
  switch (fx) {
    case FX_ALTAIR_100:
    case FX_ALTAIR_210: return side == 0 ? GG_BLUE : GG_WHITE;     // tip = blue, ring = white
    case FX_HELIX_MB:   return side == 0 ? GG_BLUE : -1;
    case FX_ATLAS: case FX_HELIX_TS: case FX_HELIX_RCD:
                        return side == 0 ? GG_WHITE : -1;
    default:            return -1;
  }
}

inline void sortPoints(Group &g) {
  for (uint8_t i = 1; i < g.n; i++) {
    Point k = g.p[i]; int j = i - 1;
    while (j >= 0 && g.p[j].min > k.min) { g.p[j + 1] = g.p[j]; j--; }
    g.p[j + 1] = k;
  }
}

// ------------------------------------------------------------------ guided schedule
// Tank type sets the overall intensity and how long the lights are on (lights-on = lights-off - photoperiod).
enum Tank : uint8_t { TANK_SOFTIE_LPS = 0, TANK_MIXED = 1, TANK_SPS = 2, TANK_COUNT };
inline uint8_t  tankIntensity(uint8_t t)   { return t == TANK_SOFTIE_LPS ? 50 : t == TANK_SPS ? 80 : 65; }
inline uint16_t tankPhotoMin(uint8_t t)    { return t == TANK_SOFTIE_LPS ? 600 : t == TANK_SPS ? 720 : 660; }  // 10 / 11 / 12 h
inline uint16_t guidedOnMin(const Config &c) { return (uint16_t)((c.offMin + 1440 - tankPhotoMin(c.tank)) % 1440); }
// Acclimation (Luca, Oct 22): absolute intensity starts 25 % lower (final x 0.75) and climbs to the final setting.
inline uint8_t  acclStartFor(uint8_t) { return 75; }
// Starting intensity by tank type. Atlas is much stronger, so it starts lower.
inline uint8_t  tankInt(uint8_t kind, uint8_t tank);
// Inside lights default 15 points under the outside ones.
inline uint8_t  insideFor(uint8_t intensity) { return (uint8_t)clampi((int)intensity - 15, 10, 100); }

// Spectrums: Luca's blue and white maps. Each point = {fraction of the photoperiod, level}, both permille;
// 0 outside the points, straight lines between. The UI (ui/index.html SPECTRA) holds the same numbers.
enum Spectrum : uint8_t { SP_SHALLOW = 0, SP_LAGOON, SP_GROW_POP, SP_MESO_FRINGE, SP_ULTRA_BLUE, SP_COUNT };
struct MapPt { uint16_t f, v; };
struct SpectrumMap { const MapPt *w; uint8_t nw; const MapPt *b; uint8_t nb; };
inline SpectrumMap spectrumMap(uint8_t sp) {
  static const MapPt shW[] = {{100,0},{200,200},{300,1000},{600,1000},{650,500},{700,500},{750,0}};
  static const MapPt shB[] = {{0,0},{100,200},{250,700},{800,700},{900,400},{1000,0}};
  static const MapPt laW[] = {{100,0},{200,200},{300,600},{350,1000},{500,1000},{550,400},{650,400},{700,0}};
  static const MapPt laB[] = {{0,0},{100,400},{200,800},{300,1000},{650,1000},{700,900},{900,800},{1000,0}};
  static const MapPt gpW[] = {{0,0},{100,400},{200,1000},{350,1000},{400,500},{500,300},{600,200},{700,0}};
  static const MapPt gpB[] = {{0,0},{100,400},{200,1000},{800,1000},{1000,0}};
  static const MapPt mfW[] = {{200,0},{300,500},{500,500},{600,0}};
  static const MapPt mfB[] = {{0,0},{100,400},{150,1000},{900,1000},{1000,0}};
  static const MapPt ubW[] = {{200,0},{350,400},{450,400},{550,0}};
  static const MapPt ubB[] = {{0,0},{50,400},{100,1000},{950,1000},{1000,0}};
  #define QC_MAP(w, b) SpectrumMap{w, (uint8_t)(sizeof(w) / sizeof(w[0])), b, (uint8_t)(sizeof(b) / sizeof(b[0]))}
  switch (sp) {
    case SP_SHALLOW:     return QC_MAP(shW, shB);
    case SP_LAGOON:      return QC_MAP(laW, laB);
    case SP_MESO_FRINGE: return QC_MAP(mfW, mfB);
    case SP_ULTRA_BLUE:  return QC_MAP(ubW, ubB);
    default:             return QC_MAP(gpW, gpB);
  }
  #undef QC_MAP
}
// A map stretched over today's photoperiod, scaled by k = num/den (inside lights), as a curve.
inline void mapCurve(Group &g, const MapPt *m, uint8_t n, uint16_t on, uint16_t photo, uint32_t num = 1, uint32_t den = 1) {
  g.n = n > MAX_POINTS ? MAX_POINTS : n;
  for (uint8_t i = 0; i < g.n; i++) {
    uint32_t lvl = ((uint32_t)m[i].v * num * 2 + den) / (den * 2);           // round half up, like the UI
    g.p[i] = {(uint16_t)((on + ((uint32_t)m[i].f * photo + 500) / 1000) % 1440), (uint16_t)(lvl > LEVEL_MAX ? LEVEL_MAX : lvl)};
  }
  sortPoints(g);
}

// The setup assistant's choices -> outputs and groups.
enum SetupKind : uint8_t { SK_ALTAIR = 0, SK_AIO, SK_SUPP, SK_ATLAS, SK_CUSTOM, SK_COUNT };
inline const char *setupKindId(uint8_t k) {
  static const char *ids[SK_COUNT] = {"altair", "aio", "supp", "atlas", "custom"};
  return k < SK_COUNT ? ids[k] : "altair";
}
inline uint8_t parTotal(const Config::Setup &s) { return s.kind == SK_ATLAS ? s.atlas : s.qty; }
// 1-2 lights: Uniform Intensity (automatic). 3-6: the user picks Uniform Intensity or Uniform PAR.
inline bool usesPar(const Config::Setup &s) { return (s.kind == SK_ALTAIR || s.kind == SK_ATLAS) && parTotal(s) >= 3 && s.par; }
// Default split when PAR is turned on: 3 -> 2+1, 4 -> 2+2, 5 -> 2+3, 6 -> 4+2 (two rows of three).
inline uint8_t defaultInside(uint8_t n) { return n <= 4 ? (uint8_t)(n > 2 ? n - 2 : 1) : n == 5 ? 3 : 2; }
inline void defaultSetup(Config::Setup &s) {
  memset(&s, 0, sizeof(s));
  s.kind = SK_ALTAIR; copyNameN(s.name, "My Reef", sizeof(s.name)); s.model = FX_ALTAIR_210; s.qty = 2; s.par = false;
  s.out = 2; s.inn = 1; s.mb = 2; s.rcd = 1; s.ts = 0; s.len = 2; s.supp = FX_HELIX_MB; s.suppQty = 2; s.atlas = 2; s.amb = 2;
}

enum Zone : uint8_t { Z_OUT = 0, Z_IN = 1, Z_MB = 2 };   // which intensity slider a group follows
struct PlanGroup { const char *name; uint8_t color; bool blue; uint8_t zone; };
struct PlanOut   { uint8_t fx; uint8_t count; uint8_t len; const char *name; int8_t grp[2]; };
// The groups and, in order, the outputs one setup needs (the aquarium's outputs are filled in this order).
struct GuidedPlan { uint8_t ng; PlanGroup g[4]; uint8_t no; PlanOut o[JACKS]; };
inline GuidedPlan guidedPlan(const Setup &s) {
  GuidedPlan p; memset(&p, 0, sizeof(p));
  auto add = [&](PlanOut o) { if (p.no < JACKS) p.o[p.no++] = o; };
  const PlanGroup B = {"Blue", COL_BLUE, true, Z_OUT}, W = {"White", COL_WHITE, false, Z_OUT};
  const PlanGroup OB = {"Outside Blue", COL_BLUE, true, Z_OUT}, OW = {"Outside White", COL_WHITE, false, Z_OUT};
  const PlanGroup IB = {"Inside Blue", COL_LT_BLUE, true, Z_IN}, IW = {"Inside White", COL_LT_WHITE, false, Z_IN};
  const PlanGroup MB = {"Meso Blue", COL_BLUE, true, Z_MB};
  const bool par = usesPar(s);
  switch (s.kind) {
    case SK_AIO:
      p.ng = 2; p.g[0] = B; p.g[1] = W;
      if (s.mb)  add({FX_HELIX_MB,  s.mb,  s.len, "Meso Blue bars",  {0, -1}});
      if (s.rcd) add({FX_HELIX_RCD, s.rcd, s.len, "Reef Crest bars", {1, -1}});
      if (s.ts)  add({FX_HELIX_TS,  s.ts,  s.len, "Tropic Sun bars", {1, -1}});
      break;
    case SK_SUPP: {
      bool blue = s.supp == FX_HELIX_MB;
      p.ng = 1; p.g[0] = blue ? B : W;
      add({s.supp, s.suppQty, s.len, "Helix bars", {0, -1}});
      break; }
    case SK_ATLAS:           // Atlas = white channel, Meso Blue bars = blue with their own intensity
      if (!par) {
        p.ng = 2; p.g[0] = MB; p.g[1] = {"Atlas", COL_WHITE, false, Z_OUT};
        add({FX_ATLAS, s.atlas, 0, "Atlas lights", {1, -1}});
        if (s.amb) add({FX_HELIX_MB, s.amb, s.len, "Meso Blue bars", {0, -1}});
      } else {
        p.ng = 3; p.g[0] = MB; p.g[1] = {"Outside Atlas", COL_WHITE, false, Z_OUT}; p.g[2] = {"Inside Atlas", COL_LT_WHITE, false, Z_IN};
        add({FX_ATLAS, s.out, 0, "Outside Atlas", {1, -1}});
        add({FX_ATLAS, s.inn, 0, "Inside Atlas", {2, -1}});
        if (s.amb) add({FX_HELIX_MB, s.amb, s.len, "Meso Blue bars", {0, -1}});
      }
      break;
    default:   // SK_ALTAIR (custom setups are expert mode)
      if (!par) {
        p.ng = 2; p.g[0] = B; p.g[1] = W;
        add({s.model, s.qty, 0, "Altairs", {0, 1}});
      } else {
        p.ng = 4; p.g[0] = OB; p.g[1] = OW; p.g[2] = IB; p.g[3] = IW;
        add({s.model, s.out, 0, "Outside Altairs", {0, 1}});
        add({s.model, s.inn, 0, "Inside Altairs", {2, 3}});
      }
  }
  return p;
}
inline uint8_t planOuts(const Setup &s) { return guidedPlan(s).no; }

// ---------- aquariums (Luca, Oct 20-22) ----------
// Starting intensity by tank type. Atlas is much stronger, so it starts at 25 / 40 / 55 %.
inline uint8_t tankInt(uint8_t kind, uint8_t tank) {
  static const uint8_t atlas[TANK_COUNT] = {25, 40, 55};
  return kind == SK_ATLAS ? atlas[tank < TANK_COUNT ? tank : 1] : tankIntensity(tank);
}
// The pages edit the mirror (c.setup, c.tank, ... c.accl*) of aquarium c.sel; these copy it in and out.
inline void aqPush(Config &c) {
  if (c.sel >= c.nAq) return;
  Aq &a = c.aq[c.sel];
  a.setup = c.setup; a.tank = c.tank; a.spectrum = c.spectrum; a.offMin = c.offMin;
  a.intensity = c.intensity; a.intenIn = c.intenIn; a.intenMB = c.intenMB;
  a.acclOn = c.acclOn; a.acclStartPct = c.acclStartPct; a.acclDays = c.acclDays; a.acclStart = c.acclStart;
}
inline void aqPull(Config &c, uint8_t i) {
  if (i >= c.nAq) return;
  const Aq &a = c.aq[i]; c.sel = i;
  c.setup = a.setup; c.tank = a.tank; c.spectrum = a.spectrum; c.offMin = a.offMin;
  c.intensity = a.intensity; c.intenIn = a.intenIn; c.intenMB = a.intenMB;
  c.acclOn = a.acclOn; c.acclStartPct = a.acclStartPct; c.acclDays = a.acclDays; c.acclStart = a.acclStart;
}
inline uint16_t aqOnMin(const Aq &a) { return (uint16_t)((a.offMin + 1440 - tankPhotoMin(a.tank)) % 1440); }
// Every intensity of the mirror set to its tank type's starting point.
inline void setTankInts(Config &c) {
  c.intensity = tankInt(c.setup.kind, c.tank); c.intenIn = insideFor(c.intensity); c.intenMB = tankIntensity(c.tank);
}
// The aquarium a guided group belongs to (nullptr in expert mode).
inline const Aq *aqOf(const Config &c, uint8_t g) {
  return (!c.expert && g < c.nGroups && c.group[g].aq < c.nAq) ? &c.aq[c.group[g].aq] : nullptr;
}
// A group's intensity: its own (inside lights, Meso Blue, an edited output), else its aquarium's.
inline uint8_t groupInt(const Config &c, uint8_t g) {
  const Aq *a = aqOf(c, g);
  if (a && c.group[g].inten) return c.group[g].inten;
  return a ? a->intensity : c.intensity;
}

inline void guidedPhotos(Config &c);
inline void groupName(Config &c, Group &G, const Aq &a, const char *pre, const char *name) {
  char buf[GNAME_LEN]; buf[0] = 0;
  if (c.nAq > 1) { strncat(buf, a.setup.name, GNAME_LEN - 1); strncat(buf, ": ", GNAME_LEN - 1 - strlen(buf)); }
  if (pre) strncat(buf, pre, GNAME_LEN - 1 - strlen(buf));
  strncat(buf, name, GNAME_LEN - 1 - strlen(buf));
  copyNameN(G.name, buf, GNAME_LEN);
}
// Rebuild every aquarium's groups (curves) and outputs from its setup, tank type, spectrum and lights-off.
// Matches applyGuided() in ui/index.html.
inline void applyGuided(Config &c) {
  if (c.nAq == 0) return;
  aqPush(c);
  const uint8_t before = c.nGroups;
  uint8_t ng = 0;
  for (uint8_t j = 0; j < JACKS; j++) {
    Jack &J = c.jack[j]; J.fixture = FX_NONE; J.count = 1; J.len = 0; J.watts[0] = J.watts[1] = 0;
    char n[NAME_LEN] = "Output 1"; n[7] = (char)('1' + j); copyName(J.name, n);
    c.ch[j * 2].group = c.ch[j * 2 + 1].group = -1;
  }
  for (uint8_t ai = 0; ai < c.nAq; ai++) {
    const Aq &a = c.aq[ai];
    const GuidedPlan p = guidedPlan(a.setup);
    const SpectrumMap m = spectrumMap(a.setup.kind == SK_SUPP ? (uint8_t)SP_LAGOON : a.spectrum);   // supplemental: Lagoon Reef
    const uint16_t on = aqOnMin(a), photo = tankPhotoMin(a.tank);
    const uint8_t base = ng;
    for (uint8_t g = 0; g < p.ng && ng < MAX_GROUPS; g++) {
      Group &G = c.group[ng++];
      groupName(c, G, a, nullptr, p.g[g].name); G.color = p.g[g].color; G.aq = ai; G.role = p.g[g].blue ? 0 : 1;
      G.inten = p.g[g].zone == Z_IN ? a.intenIn : p.g[g].zone == Z_MB ? a.intenMB : 0;
      if (p.g[g].blue) mapCurve(G, m.b, m.nb, on, photo); else mapCurve(G, m.w, m.nw, on, photo);
    }
    for (uint8_t k = 0; k < p.no && k < a.nOut; k++) {
      const PlanOut &o = p.o[k]; const uint8_t j = a.out[k]; if (j >= JACKS) continue;
      Jack &J = c.jack[j];
      J.fixture = o.fx; J.count = o.count ? o.count : 1; J.len = o.len;
      copyName(J.name, a.outName[k][0] ? a.outName[k] : o.name);
      const OutAdj &adj = a.adj[k];
      for (uint8_t s = 0; s < 2; s++) {
        J.watts[s] = fixtureWatts(o.fx, s, o.len);
        int8_t g = o.grp[s] < 0 ? -1 : (int8_t)(base + o.grp[s]);
        if (g >= ng) g = -1;
        if (g >= 0 && adj.on && ng < MAX_GROUPS) {      // this output's own times and intensity: the curve stretched into its window
          const Group &src = c.group[g]; Group &G = c.group[ng];
          char pre[12] = "OUT 1 "; pre[4] = (char)('1' + j);
          const char *nm = strrchr(src.name, ':'); nm = nm ? nm + 2 : src.name;
          groupName(c, G, a, pre, nm); G.color = src.color; G.aq = ai; G.role = src.role; G.inten = adj.inten[s] ? adj.inten[s] : 0;
          uint16_t len = (uint16_t)(((adj.offMin + 1440 - adj.onMin) % 1440) ? (adj.offMin + 1440 - adj.onMin) % 1440 : 1440);
          G.n = src.n;
          for (uint8_t i = 0; i < src.n; i++) {
            uint32_t rel = (uint32_t)((src.p[i].min + 1440 - on) % 1440);
            G.p[i] = {(uint16_t)((adj.onMin + (rel * len + photo / 2) / photo) % 1440), src.p[i].lvl};
          }
          sortPoints(G); g = (int8_t)ng++;
        }
        c.ch[j * 2 + s].group = g;
      }
    }
  }
  c.nGroups = ng ? ng : 1;
  for (uint8_t g = ng; g < MAX_GROUPS; g++) { c.group[g].n = 0; c.group[g].aq = NO_AQ; c.group[g].inten = 0; }
  if (before != c.nGroups) guidedPhotos(c);
}
// Picture modes (Luca, Oct 22): Coral Glow, True Color and Vibrant Fish for each aquarium. Levels are shares of each
// channel's own intensity; the other aquariums' groups keep following their schedules (FOLLOW).
inline void guidedPhotos(Config &c) {
  static const char *names[3] = {"Coral Glow", "True Color", "Vibrant Fish"};
  static const uint16_t blueK[3] = {1000, 600, 850}, whiteK[3] = {150, 1000, 1000};   // permille
  c.nPhoto = 0;
  for (uint8_t p = 0; p < MAX_PHOTO; p++) { c.photo[p].minutes = 30; c.photo[p].aq = NO_AQ; }
  const uint8_t naq = (!c.expert && c.nAq) ? c.nAq : 1;
  for (uint8_t ai = 0; ai < naq; ai++)
    for (uint8_t k = 0; k < 3 && c.nPhoto < MAX_PHOTO; k++) {
      Photo &P = c.photo[c.nPhoto++];
      copyName(P.name, names[k]); P.minutes = 30; P.aq = (!c.expert && c.nAq) ? ai : NO_AQ;
      for (uint8_t g = 0; g < MAX_GROUPS; g++) {
        if (g >= c.nGroups) { P.lvl[g] = 0; continue; }
        if (P.aq != NO_AQ && c.group[g].aq != ai) { P.lvl[g] = FOLLOW; continue; }
        bool blue = c.group[g].role == 0;
        P.lvl[g] = (uint16_t)(((uint32_t)(blue ? blueK[k] : whiteK[k]) * groupInt(c, g) + 50) / 100);
      }
    }
}
// A config without a saved setup (older firmware): describe what's plugged in as well as we can.
inline void setupFromJacks(Config &c) {
  defaultSetup(c.setup);
  for (uint8_t j = 0; j < JACKS; j++) {
    uint8_t fx = c.jack[j].fixture;
    if (fx == FX_ALTAIR_100 || fx == FX_ALTAIR_210) { c.setup.model = fx; c.setup.qty = (uint8_t)clampi(c.jack[j].count, 1, 6); break; }
  }
}
// One aquarium from the mirror, on the first outputs (older settings, or a brand-new box).
inline void singleAq(Config &c) {
  memset(c.aq, 0, sizeof(c.aq));
  c.nAq = 1; c.sel = 0;
  Aq &a = c.aq[0];
  a.nOut = planOuts(c.setup);
  for (uint8_t k = 0; k < a.nOut; k++) a.out[k] = k;
  aqPush(c);
}
// Keep the setup's numbers inside what the assistant allows. Returns the number of fields fixed.
inline int sanitizeSetup(Setup &s) {
  int fixes = 0;
  auto fix = [&](bool bad) { if (bad) fixes++; return bad; };
  auto lim = [&](uint8_t &v, int lo, int hi) { if (fix(v < lo || v > hi)) v = (uint8_t)clampi(v, lo, hi); };
  if (fix(s.kind >= SK_CUSTOM)) s.kind = SK_ALTAIR;            // custom setups run in expert mode
  s.name[sizeof(s.name) - 1] = 0;
  if (fix(s.name[0] == 0)) copyNameN(s.name, "My Reef", sizeof(s.name));
  if (fix(s.model != FX_ALTAIR_100 && s.model != FX_ALTAIR_210)) s.model = FX_ALTAIR_210;
  if (fix(s.supp != FX_HELIX_MB && s.supp != FX_HELIX_RCD && s.supp != FX_HELIX_TS)) s.supp = FX_HELIX_MB;
  lim(s.qty, 1, 6); lim(s.atlas, 1, 6); lim(s.suppQty, 1, 6);
  lim(s.mb, 0, 6); lim(s.rcd, 0, 6); lim(s.ts, 0, 6); lim(s.amb, 0, 6); lim(s.len, 0, HELIX_LENS - 1);
  if (fix(s.kind == SK_AIO && s.mb + s.rcd + s.ts == 0)) s.mb = 1;
  uint8_t n = parTotal(s);
  if (usesPar(s)) {
    lim(s.inn, 1, n - 1);
    if (fix(s.out != n - s.inn)) s.out = n - s.inn;
  }
  return fixes;
}
// Aquariums: each one's numbers in range, outputs real and not shared. Returns fields fixed.
inline int sanitizeAqs(Config &c) {
  int fixes = 0;
  auto fix = [&](bool bad) { if (bad) fixes++; return bad; };
  if (fix(c.nAq < 1 || c.nAq > MAX_AQ)) singleAq(c);            // unreadable: one aquarium from the page's setup
  if (fix(c.sel >= c.nAq)) c.sel = 0;
  uint8_t used = 0;
  for (uint8_t i = 0; i < c.nAq; i++) {
    Aq &a = c.aq[i];
    fixes += sanitizeSetup(a.setup);
    if (fix(a.tank >= TANK_COUNT)) a.tank = TANK_MIXED;
    if (fix(a.spectrum >= SP_COUNT)) a.spectrum = SP_GROW_POP;
    if (fix(a.setup.kind == SK_ATLAS && a.spectrum > SP_GROW_POP)) a.spectrum = SP_GROW_POP;   // Atlas: no blue-heavy spectrums
    if (fix(a.offMin > 1439)) a.offMin = 1439;
    if (fix(a.intensity < 10 || a.intensity > 100)) a.intensity = (uint8_t)clampi(a.intensity, 10, 100);
    if (fix(a.intenIn < 10 || a.intenIn > 100)) a.intenIn = (uint8_t)clampi(a.intenIn, 10, 100);
    if (fix(a.intenMB < 10 || a.intenMB > 100)) a.intenMB = (uint8_t)clampi(a.intenMB, 10, 100);
    if (fix(a.acclStartPct < 10 || a.acclStartPct > 100)) a.acclStartPct = (uint8_t)clampi(a.acclStartPct, 10, 100);
    if (fix(a.acclDays < 1 || a.acclDays > 120)) a.acclDays = (uint16_t)clampi(a.acclDays, 1, 120);
    const uint8_t need = planOuts(a.setup);
    if (fix(a.nOut > JACKS)) a.nOut = JACKS;
    uint8_t k = 0;
    for (uint8_t x = 0; x < a.nOut; x++) {            // drop outputs that don't exist or another aquarium has
      uint8_t j = a.out[x];
      if (j >= JACKS || (used & (1 << j))) { fixes++; continue; }
      a.out[k] = j; a.adj[k] = a.adj[x]; memcpy(a.outName[k], a.outName[x], NAME_LEN); used |= 1 << j; k++;
    }
    for (uint8_t j = 0; j < JACKS && k < need; j++) if (!(used & (1 << j))) { a.out[k] = j; a.adj[k] = {}; a.outName[k][0] = 0; used |= 1 << j; k++; fixes++; }
    if (fix(k > need)) { for (uint8_t x = need; x < k; x++) used &= ~(1 << a.out[x]); k = need; }
    a.nOut = k;
    for (uint8_t x = 0; x < JACKS; x++) {
      a.outName[x][NAME_LEN - 1] = 0;
      OutAdj &d = a.adj[x];
      if (fix(d.onMin > 1439)) d.onMin = 1439;
      if (fix(d.offMin > 1439)) d.offMin = 1439;
      for (uint8_t s = 0; s < 2; s++) if (fix(d.inten[s] > 100)) d.inten[s] = 100;
    }
  }
  return fixes;
}

// ------------------------------------------------------------------ curves

// Linear interpolation between points, wrapping at midnight. Seconds for smooth ramps.
inline uint16_t curveAt(const Group &g, uint32_t secOfDay) {
  if (g.n == 0) return 0;
  if (g.n == 1) return g.p[0].lvl;
  int32_t t = (int32_t)(secOfDay % DAY);
  int8_t a = g.n - 1;
  for (uint8_t i = 0; i < g.n; i++) if ((int32_t)g.p[i].min * 60 <= t) a = i;
  uint8_t b = (a + 1) % g.n;
  int32_t ta = g.p[a].min * 60, tb = g.p[b].min * 60;
  if (tb <= ta) tb += DAY;
  int32_t tt = t < ta ? t + (int32_t)DAY : t;
  int32_t span = tb - ta;
  int64_t f = span ? (int64_t)(tt - ta) * 1000 / span : 0;
  return (uint16_t)(g.p[a].lvl + ((int32_t)g.p[b].lvl - (int32_t)g.p[a].lvl) * f / 1000);
}

// Acclimation factor in permille for a given time.
inline uint16_t acclFactorRaw(bool on, uint8_t startPct, uint16_t days, uint32_t start, uint32_t epoch) {
  if (!on || days == 0) return 1000;
  if (epoch <= start) return startPct * 10;
  uint32_t el = epoch - start, tot = (uint32_t)days * DAY;
  if (el >= tot) return 1000;
  return (uint16_t)(startPct * 10 + (uint64_t)(1000 - startPct * 10) * el / tot);
}
inline uint16_t acclFactor(const Config &c, uint32_t epoch) { return acclFactorRaw(c.acclOn, c.acclStartPct, c.acclDays, c.acclStart, epoch); }
// A group's acclimation: its aquarium's (each aquarium acclimates on its own).
inline uint16_t groupAccl(const Config &c, uint8_t g, uint32_t epoch) {
  const Aq *a = aqOf(c, g);
  return a ? acclFactorRaw(a->acclOn, a->acclStartPct, a->acclDays, a->acclStart, epoch) : acclFactor(c, epoch);
}

// ------------------------------------------------------------------ modes
enum Mode : uint8_t { MODE_AUTO = 0, MODE_MANUAL = 1, MODE_PHOTO = 2 };
struct Override {
  uint8_t  mode = MODE_AUTO;
  uint32_t until = 0;              // epoch it ends; 1 = clock unknown (the uptime counter ends it)
  int8_t   photo = -1;             // which picture setting is showing
  uint8_t  mins = 0;               // 30 or 60: how long it lasts
  uint16_t lvl[MAX_GROUPS] = {0};
};

// Manual and picture mode only ever last 30 min or 1 h, so nobody forgets and cooks their corals.
inline uint8_t overrideMinutes(int m) { return m > 30 ? 60 : 30; }

// Is the override still in force? Both modes always have an end.
inline bool overrideActive(const Override &o, bool timeValid, uint32_t epoch) {
  if (o.mode == MODE_AUTO) return false;
  if (o.until == 0) return false;                // no end recorded: not allowed any more
  if (!timeValid) return true;                   // can't tell time: keep what the user chose
  return epoch < o.until;
}

// Levels (permille) for every group right now. Returns the mode in force.
inline uint8_t groupLevels(const Config &c, const Override &o, bool timeValid, uint32_t epoch,
                           uint32_t secOfDay, uint16_t out[MAX_GROUPS]) {
  for (uint8_t g = 0; g < MAX_GROUPS; g++) {
    if (g >= c.nGroups) { out[g] = 0; continue; }
    if (!timeValid) { out[g] = NO_CLOCK_LEVEL; continue; }
    uint32_t v = curveAt(c.group[g], secOfDay);
    v = v * groupInt(c, g) / 100;
    v = v * groupAccl(c, g, epoch) / 1000;
    out[g] = (uint16_t)(v > LEVEL_MAX ? LEVEL_MAX : v);
  }
  if (overrideActive(o, timeValid, epoch)) {           // manual / picture: groups it names; FOLLOW keeps the schedule
    for (uint8_t g = 0; g < MAX_GROUPS; g++) if (o.lvl[g] != FOLLOW) out[g] = o.lvl[g] > LEVEL_MAX ? LEVEL_MAX : o.lvl[g];
    return o.mode;
  }
  return MODE_AUTO;
}

inline bool channelInUse(const Config &c, uint8_t ch) {
  if (ch >= CHANNELS) return false;
  uint8_t fx = c.jack[ch / 2].fixture;
  if (fx == FX_NONE) return false;
  if (fx == FX_OTHER) return c.ch[ch].group >= 0;    // 3rd party: only the sides the user assigned
  if ((ch & 1) && !twoChannel(fx)) return false;     // single-channel products: ring held at 0 V
  return true;
}

// Final level of one output in permille, after trim and the minimum-dim floor.
inline uint16_t channelLevel(const Config &c, const uint16_t grp[MAX_GROUPS], uint8_t ch) {
  if (!channelInUse(c, ch)) return 0;
  int8_t g = c.ch[ch].group;
  if (g < 0 || g >= c.nGroups) return 0;
  uint32_t v = (uint32_t)grp[g] * c.ch[ch].trim / 100;
  if (v == 0) return 0;
  uint32_t fl = (uint32_t)c.floorPct * 10;
  if (v < fl) v = fl;
  return (uint16_t)(v > LEVEL_MAX ? LEVEL_MAX : v);
}

// Output hardware: DAC 0..2.048 V (12-bit, internal ref, gain 1) -> op-amp gain 4.9 -> 0..10.03 V.
// cal (80..120 %) trims each output so 100 % reads exactly 10.00 V on a meter.
constexpr float DAC_FS_V = 2.048f, AMP_GAIN = 4.9f;           // 39k / 10k + 1
constexpr float OUT_FS_V = DAC_FS_V * AMP_GAIN;                  // 10.035 V at code 4095
inline uint16_t levelToCode(uint16_t lvl, uint8_t cal) {
  if (lvl == 0) return 0;
  float v = lvl / 1000.0f * 10.0f * cal / 100.0f;                // wanted volts
  int code = (int)(v / OUT_FS_V * DAC_MAX + 0.5f);
  return (uint16_t)clampi(code, 0, DAC_MAX);
}
inline uint16_t codeToMilliVolts(uint16_t code) { return (uint16_t)(code * OUT_FS_V * 1000.0f / DAC_MAX + 0.5f); }
// What the jack really shows for a DAC code once the output's calibration is applied. cal exists because
// the real amplifier gain is off by a percent or two, so the nominal value above is off by the same amount.
inline uint16_t jackMilliVolts(uint16_t code, uint8_t cal) {
  if (cal < 80 || cal > 120) cal = 100;
  return (uint16_t)((uint32_t)codeToMilliVolts(code) * 100 / cal);
}

// Move `cur` toward `tgt` by at most fullRange/fadeMs per ms. Levels are permille.
inline uint16_t slew(uint16_t cur, uint16_t tgt, uint32_t dtMs, uint32_t fadeMs) {
  if (fadeMs == 0 || cur == tgt) return tgt;
  uint32_t step = (uint32_t)LEVEL_MAX * dtMs / fadeMs;
  if (step == 0) step = 1;
  if (tgt > cur) return (uint16_t)((uint32_t)(tgt - cur) <= step ? tgt : cur + step);
  return (uint16_t)((uint32_t)(cur - tgt) <= step ? tgt : cur - step);
}

// ------------------------------------------------------------------ output monitoring
// Each output has a 1k resistor between the op-amp and the jack; the op-amp's feedback comes from
// the jack side, so the jack voltage is correct regardless of load. Measuring the op-amp side tells
// us the current the fixtures push back into the controller (Mean Well dim inputs source ~0.1-0.2 mA)
// and whether the jack is shorted (the op-amp runs to its rail trying to raise a dead-short jack).
enum OutStatus : uint8_t { OUT_IDLE = 0, OUT_OK = 1, OUT_NO_LOAD = 2, OUT_SHORT = 3, OUT_UNKNOWN = 4 };
constexpr uint16_t SERIES_OHMS = 1000;
// Zeroing the monitor: with nothing plugged in, the op-amp side must equal the jack voltage. 1 % divider
// resistors can be ~1.5 % off together, which reads as ~+/-75 uA at 5 V -- as big as a real driver -- so each
// channel stores a correction (permille). Returns 0 if the reading is too far off to be a clean no-load reading.
constexpr uint16_t MON_GAIN_MIN = 900, MON_GAIN_MAX = 1100;
inline uint16_t monitorGainFor(uint16_t jackMv, int rawAmpMv) {
  if (jackMv < 2000 || rawAmpMv <= 0) return 0;
  uint32_t g = ((uint32_t)jackMv * 1000 + (uint32_t)rawAmpMv / 2) / (uint32_t)rawAmpMv;
  return (g < MON_GAIN_MIN || g > MON_GAIN_MAX) ? 0 : (uint16_t)g;
}
inline int applyMonitorGain(int rawAmpMv, uint16_t gain) {
  if (rawAmpMv < 0) return rawAmpMv;
  if (gain < MON_GAIN_MIN || gain > MON_GAIN_MAX) gain = 1000;
  return (int)(((int32_t)rawAmpMv * gain + 500) / 1000);
}
struct OutReading { uint8_t status; int16_t microAmps; };
inline OutReading classifyOutput(uint16_t setMv, uint16_t ampMv, bool inUse) {
  OutReading r{OUT_IDLE, 0};
  int32_t ua = ((int32_t)setMv - (int32_t)ampMv) * 1000 / SERIES_OHMS;   // + = fixture sourcing
  r.microAmps = (int16_t)clampi(ua, -32000, 32000);
  if (!inUse) return r;
  if (ampMv > setMv + 1500 && setMv >= 500) { r.status = OUT_SHORT; return r; }
  if (setMv < 1000 || setMv > 9000) { r.status = OUT_UNKNOWN; return r; } // drivers only measurable mid-range
  r.status = ua >= 40 ? OUT_OK : OUT_NO_LOAD;
  return r;
}

// ------------------------------------------------------------------ defaults & validation
inline void defaultConfig(Config &c) {
  memset(&c, 0, sizeof(c));
  c.tank = TANK_MIXED; c.spectrum = SP_GROW_POP; c.offMin = 22 * 60;      // lights off 10:00 PM by default
  c.intensity = tankIntensity(c.tank); c.intenIn = insideFor(c.intensity);
  for (uint8_t j = 0; j < JACKS; j++) c.jack[j].count = 1;
  for (uint8_t i = 0; i < CHANNELS; i++) c.ch[i] = {-1, 100, 100};
  defaultSetup(c.setup); c.intenMB = tankIntensity(c.tank);
  c.acclStartPct = acclStartFor(c.intensity); c.acclDays = 30;
  singleAq(c);
  c.nGroups = 0; applyGuided(c); guidedPhotos(c);
  c.floorPct = 10; c.rampSec = 60; c.fadeSec = 3;
  c.acclOn = false; c.acclStartPct = acclStartFor(c.intensity); c.acclDays = 30; c.acclStart = 0;   // the assistant turns it on
  strcpy(c.tz, "CST6CDT,M3.2.0,M11.1.0");
  c.expert = false; c.setupDone = false; c.costMils = 150;
}

// Apply a product to a jack: channels and watts fill in by themselves.
inline void setFixture(Config &c, uint8_t j, uint8_t fx, uint8_t len = 0) {
  if (j >= JACKS) return;
  Jack &J = c.jack[j];
  J.fixture = fx < FX_COUNT ? fx : (uint8_t)FX_NONE;
  J.len = isHelix(J.fixture) && len < HELIX_LENS ? len : 0;
  for (uint8_t s = 0; s < 2; s++) {
    int8_t g = defaultGroup(J.fixture, s);
    c.ch[j * 2 + s].group = (g >= 0 && g < c.nGroups) ? g : -1;
    if (J.fixture != FX_OTHER) J.watts[s] = fixtureWatts(J.fixture, s, J.len);
  }
}

// Make any config safe to run: clamp everything, fix references, sort curves.
// Returns the number of fields it had to correct (0 = config was already clean).
inline int sanitize(Config &c) {
  int fixes = 0;
  auto fix = [&](bool bad) { if (bad) fixes++; return bad; };
  if (fix(c.nGroups < 1 || c.nGroups > MAX_GROUPS)) c.nGroups = c.nGroups < 1 ? 1 : MAX_GROUPS;
  for (uint8_t g = 0; g < MAX_GROUPS; g++) {
    Group &G = c.group[g];
    G.name[GNAME_LEN - 1] = 0;
    if (fix(G.n > MAX_POINTS)) G.n = MAX_POINTS;
    if (fix(G.inten > 100)) G.inten = 100;
    if (fix(G.role > 1)) G.role = 0;
    for (uint8_t i = 0; i < G.n; i++) {
      if (fix(G.p[i].min > 1439)) G.p[i].min = 1439;
      if (fix(G.p[i].lvl > LEVEL_MAX)) G.p[i].lvl = LEVEL_MAX;
    }
    sortPoints(G);
    if (fix(G.color >= COL_COUNT)) G.color = COL_GREEN;
  }
  for (uint8_t j = 0; j < JACKS; j++) {
    c.jack[j].name[NAME_LEN - 1] = 0;
    if (fix(c.jack[j].fixture >= FX_COUNT)) c.jack[j].fixture = FX_NONE;
    if (fix(c.jack[j].count < 1 || c.jack[j].count > 8)) c.jack[j].count = c.jack[j].count < 1 ? 1 : 8;
    if (fix(c.jack[j].len >= HELIX_LENS || (c.jack[j].len && !isHelix(c.jack[j].fixture)))) c.jack[j].len = 0;
    for (uint8_t s = 0; s < 2; s++) {
      uint8_t fx = c.jack[j].fixture;
      if (fx == FX_OTHER) { if (fix(c.jack[j].watts[s] > 1000)) c.jack[j].watts[s] = 1000; }
      else if (fix(c.jack[j].watts[s] != fixtureWatts(fx, s, c.jack[j].len))) c.jack[j].watts[s] = fixtureWatts(fx, s, c.jack[j].len);
    }
  }
  if (fix(c.tank >= TANK_COUNT)) c.tank = TANK_MIXED;
  if (fix(c.spectrum >= SP_COUNT)) c.spectrum = SP_GROW_POP;
  if (fix(c.offMin > 1439)) c.offMin = 1439;
  if (fix(c.intensity < 10 || c.intensity > 100)) c.intensity = (uint8_t)clampi(c.intensity, 10, 100);
  if (fix(c.intenIn < 10 || c.intenIn > 100)) c.intenIn = (uint8_t)clampi(c.intenIn, 10, 100);
  // Guided mode: the outputs and curves always follow the setup assistant's answers.
  if (fix(c.intenMB < 10 || c.intenMB > 100)) c.intenMB = (uint8_t)clampi(c.intenMB, 10, 100);
  if (!c.expert) {
    fixes += sanitizeSetup(c.setup);
    if (fix(c.setup.kind == SK_ATLAS && c.spectrum > SP_GROW_POP)) c.spectrum = SP_GROW_POP;   // Atlas: no blue-heavy spectrums
    if (c.nAq >= 1 && c.nAq <= MAX_AQ && c.sel < c.nAq) aqPush(c);   // the pages' edits go into the aquarium they're on
  }
  fixes += sanitizeAqs(c);
  if (!c.expert) { aqPull(c, c.sel); applyGuided(c); }
  for (uint8_t i = 0; i < CHANNELS; i++) {
    if (fix(c.ch[i].group >= (int8_t)c.nGroups || c.ch[i].group < -1)) c.ch[i].group = -1;
    if (fix(c.ch[i].trim > 100)) c.ch[i].trim = 100;
    if (fix(c.ch[i].cal < 80 || c.ch[i].cal > 120)) c.ch[i].cal = (uint8_t)clampi(c.ch[i].cal, 80, 120);
  }
  if (fix(c.nPhoto > MAX_PHOTO)) c.nPhoto = MAX_PHOTO;
  for (uint8_t p = 0; p < MAX_PHOTO; p++) {
    c.photo[p].name[NAME_LEN - 1] = 0;
    if (fix(c.photo[p].minutes != 30 && c.photo[p].minutes != 60)) c.photo[p].minutes = overrideMinutes(c.photo[p].minutes);
    for (uint8_t g = 0; g < MAX_GROUPS; g++) if (fix(c.photo[p].lvl[g] > LEVEL_MAX && c.photo[p].lvl[g] != FOLLOW)) c.photo[p].lvl[g] = LEVEL_MAX;
    if (fix(c.photo[p].aq != NO_AQ && c.photo[p].aq >= c.nAq)) c.photo[p].aq = NO_AQ;
  }
  if (fix(c.floorPct > 40)) c.floorPct = 40;
  if (fix(c.rampSec > 1800)) c.rampSec = 1800;
  if (fix(c.fadeSec > 60)) c.fadeSec = 60;
  if (fix(c.acclStartPct < 10 || c.acclStartPct > 100)) c.acclStartPct = (uint8_t)clampi(c.acclStartPct, 10, 100);
  if (fix(c.acclDays < 1 || c.acclDays > 120)) c.acclDays = (uint16_t)clampi(c.acclDays, 1, 120);
  c.tz[sizeof(c.tz) - 1] = 0;
  if (fix(c.tz[0] == 0)) strcpy(c.tz, "UTC0");
  return fixes;
}

// ------------------------------------------------------------------ misc
inline uint32_t crc32(const uint8_t *d, uint32_t n, uint32_t crc = 0) {
  crc = ~crc;
  while (n--) { crc ^= *d++; for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1))); }
  return ~crc;
}

}  // namespace qc
