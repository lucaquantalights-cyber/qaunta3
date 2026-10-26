// Host-side tests for core.h:  g++ -std=c++17 -Wall -Wextra -I../src test_core.cpp -o t && ./t
#include "core.h"
#include <cstdio>

using namespace qc;
static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main() {
  Config c; defaultConfig(c);
  CHECK(sanitize(c) == 0);                                  // defaults are clean

  // ---- guided defaults: 2 Altair 210s on OUT 1, Mixed tank (11 h), Grow & Pop, lights off 22:00 -> on 11:00
  CHECK(c.nGroups == 2 && c.intensity == 65 && c.intenIn == 50 && guidedOnMin(c) == 660 && c.offMin == 22 * 60);
  CHECK(c.setup.kind == SK_ALTAIR && c.setup.qty == 2 && !usesPar(c.setup) && !strcmp(c.setup.name, "My Reef"));
  CHECK(c.jack[0].fixture == FX_ALTAIR_210 && c.jack[0].count == 2 && c.jack[1].fixture == FX_NONE);
  CHECK(c.ch[0].group == GG_BLUE && c.ch[1].group == GG_WHITE && c.ch[2].group == -1);
  CHECK(!strcmp(c.group[0].name, "Blue") && !strcmp(c.group[1].name, "White") && c.nPhoto == 3);
  CHECK(c.nAq == 1 && c.sel == 0 && c.aq[0].nOut == 1 && c.aq[0].out[0] == 0 && c.group[0].aq == 0 && c.group[1].role == 1);
  CHECK(curveAt(c.group[GG_BLUE], 15 * 3600) == 1000 && curveAt(c.group[GG_WHITE], 14 * 3600) == 1000);
  CHECK(curveAt(c.group[GG_BLUE], 21 * 3600) > 0 && curveAt(c.group[GG_WHITE], 21 * 3600) == 0);   // blues stay on later
  CHECK(curveAt(c.group[GG_BLUE], 3 * 3600) == 0 && curveAt(c.group[GG_BLUE], 23 * 3600) == 0);
  CHECK(tankIntensity(TANK_SOFTIE_LPS) == 50 && tankIntensity(TANK_SPS) == 80);
  CHECK(tankPhotoMin(TANK_SOFTIE_LPS) == 600 && tankPhotoMin(TANK_MIXED) == 660 && tankPhotoMin(TANK_SPS) == 720);
  CHECK(acclStartFor(65) == 75 && acclStartFor(50) == 75);   // Luca, Oct 22: start 25 % lower (50 % -> 37.5 %)
  CHECK(tankInt(SK_ATLAS, TANK_SOFTIE_LPS) == 25 && tankInt(SK_ATLAS, TANK_MIXED) == 40 && tankInt(SK_ATLAS, TANK_SPS) == 55 && tankInt(SK_ALTAIR, TANK_SPS) == 80);
  CHECK(insideFor(65) == 50 && insideFor(20) == 10);
  CHECK(defaultInside(3) == 1 && defaultInside(4) == 2 && defaultInside(5) == 3 && defaultInside(6) == 2);
  { Config s2 = c; s2.spectrum = SP_ULTRA_BLUE; s2.tank = TANK_SPS; sanitize(s2);   // SPS: 12 h, on 10:00
    CHECK(guidedOnMin(s2) == 10 * 60 && curveAt(s2.group[GG_WHITE], (10 * 60 + 288) * 60) == 400); }

  // ---- Uniform PAR Mode: 4 Altairs -> 2 outside on OUT 1, 2 inside on OUT 2, inside runs at intenIn
  { Config p = c; p.setup.qty = 4; p.setup.par = true; p.setup.inn = 9; p.setup.out = 0;
    CHECK(sanitize(p) >= 2 && p.setup.inn == 3 && p.setup.out == 1 && p.aq[0].nOut == 2 && p.aq[0].out[1] == 1);   // split kept inside 1..qty-1
    p.setup.inn = defaultInside(4); sanitize(p);
    CHECK(p.nGroups == 4 && p.setup.out == 2 && p.jack[0].count == 2 && p.jack[1].count == 2 && p.jack[1].fixture == FX_ALTAIR_210);
    CHECK(p.ch[0].group == 0 && p.ch[1].group == 1 && p.ch[2].group == 2 && p.ch[3].group == 3);
    CHECK(!strcmp(p.group[2].name, "Inside Blue") && p.group[3].color == COL_LT_WHITE);
    CHECK(curveAt(p.group[2], 15 * 3600) == 1000 && groupInt(p, 2) == 50 && groupInt(p, 0) == 65);   // inside: own intensity
    { Override o2; uint16_t l2[MAX_GROUPS]; groupLevels(p, o2, true, 0, 15 * 3600, l2); CHECK(l2[0] == 650 && l2[2] == 500); }
    CHECK(p.photo[0].lvl[2] == 500 && p.photo[0].lvl[3] == 75 && p.nPhoto == 3 && !strcmp(p.photo[2].name, "Vibrant Fish"));
    p.setup.qty = 2; sanitize(p); CHECK(p.nGroups == 2 && p.jack[1].fixture == FX_NONE); }   // 1-2 lights: always uniform
  // ---- Helix AIO: MB on OUT 1 (blue), RCD on OUT 2, TS on OUT 3 (white)
  { Config a = c; a.setup.kind = SK_AIO; a.setup.mb = 2; a.setup.rcd = 1; a.setup.ts = 1; a.setup.len = 3; sanitize(a);
    CHECK(a.jack[0].fixture == FX_HELIX_MB && a.jack[1].fixture == FX_HELIX_RCD && a.jack[2].fixture == FX_HELIX_TS);
    CHECK(a.ch[0].group == 0 && a.ch[2].group == 1 && a.ch[4].group == 1 && a.ch[1].group == -1 && a.jack[1].watts[0] == 92);
    a.setup.mb = a.setup.rcd = a.setup.ts = 0; CHECK(sanitize(a) >= 1 && a.setup.mb == 1 && a.aq[0].nOut == 1); }
  // ---- Helix supplemental: one group, Lagoon Reef curves whatever the spectrum
  { Config h = c; h.setup.kind = SK_SUPP; h.setup.supp = FX_HELIX_RCD; h.setup.suppQty = 3; sanitize(h);
    CHECK(h.nGroups == 1 && !strcmp(h.group[0].name, "White") && h.jack[0].fixture == FX_HELIX_RCD && h.jack[0].count == 3);
    CHECK(h.ch[0].group == 0 && h.ch[1].group == -1 && h.jack[1].fixture == FX_NONE);
    CHECK(curveAt(h.group[0], (660 + 264) * 60) == 1000); }                      // Lagoon white plateau (.35-.50)
  // ---- Atlas & Helix: Atlas = white, MB = blue; PAR -> outside / inside Atlas on OUT 1 / 2, MB on OUT 3
  { Config t = c; t.setup.kind = SK_ATLAS; t.setup.atlas = 2; t.setup.amb = 2; t.spectrum = SP_ULTRA_BLUE;
    CHECK(sanitize(t) >= 1 && t.spectrum == SP_GROW_POP);                       // Atlas: Shallow, Lagoon, Grow & Pop only
    CHECK(t.jack[0].fixture == FX_ATLAS && t.ch[0].group == 1 && t.jack[1].fixture == FX_HELIX_MB && t.ch[2].group == 0);
    t.setup.atlas = 3; t.setup.par = true; t.setup.inn = 1; sanitize(t);
    CHECK(t.nGroups == 3 && t.jack[0].count == 2 && t.jack[1].count == 1 && t.ch[2].group == 2 && t.jack[2].fixture == FX_HELIX_MB && t.ch[4].group == 0);
    // Oct 22: Atlas 25/40/55 %, Meso Blue bars on their own slider
    t.tank = TANK_SPS; setTankInts(t); sanitize(t);
    CHECK(t.intensity == 55 && t.intenIn == 40 && t.intenMB == 80 && groupInt(t, 0) == 80 && groupInt(t, 1) == 55 && groupInt(t, 2) == 40);
    CHECK(!strcmp(t.group[0].name, "Meso Blue") && !strcmp(t.group[1].name, "Outside Atlas") && !strcmp(t.group[2].name, "Inside Atlas")); }
  // ---- two aquariums (Luca's Office Reef + Ultimate SPS REEF): Altairs on OUT 1, Atlas & Helix PAR on OUT 2-4
  { Config d = c;
    copyNameN(d.setup.name, "Office Reef", sizeof(d.setup.name)); aqPush(d);
    d.nAq = 2; d.aq[1] = d.aq[0]; Aq &u = d.aq[1];
    u.setup.kind = SK_ATLAS; u.setup.atlas = 3; u.setup.par = true; u.setup.inn = 1; u.setup.amb = 2; copyNameN(u.setup.name, "Ultimate SPS REEF", sizeof(u.setup.name));
    u.tank = TANK_SPS; u.spectrum = SP_LAGOON; u.intensity = 55; u.intenIn = 40; u.intenMB = 80; u.nOut = 0;   // outputs filled in by sanitize
    CHECK(sanitize(d) > 0 && d.nGroups == 5 && d.aq[1].nOut == 3 && d.aq[1].out[0] == 1 && d.aq[1].out[2] == 3);
    CHECK(d.jack[0].fixture == FX_ALTAIR_210 && d.jack[1].fixture == FX_ATLAS && d.jack[3].fixture == FX_HELIX_MB);
    CHECK(d.ch[0].group == 0 && d.ch[2].group == 3 && d.ch[4].group == 4 && d.ch[6].group == 2 && d.group[2].aq == 1);
    CHECK(!strcmp(d.group[0].name, "Office Reef: Blue") && !strcmp(d.group[3].name, "Ultimate SPS REEF: Outside Atlas"));
    CHECK(d.nPhoto == 6 && d.photo[3].aq == 1 && d.photo[3].lvl[0] == FOLLOW && d.photo[3].lvl[2] == 800);   // Coral Glow for Ultimate: MB 100 % of 80
    CHECK(sanitize(d) == 0);
    // the pages edit aquarium 2 through the mirror
    aqPull(d, 1); CHECK(d.sel == 1 && d.intensity == 55 && d.setup.kind == SK_ATLAS);
    d.intensity = 50; sanitize(d); CHECK(d.aq[1].intensity == 50 && groupInt(d, 3) == 50 && d.aq[0].intensity == 65);
    // each aquarium acclimates on its own
    d.acclOn = true; d.acclStartPct = 75; d.acclDays = 30; d.acclStart = 1000; sanitize(d);
    uint16_t l3[MAX_GROUPS]; Override o3; groupLevels(d, o3, true, 1000, 15 * 3600, l3);
    CHECK(l3[0] == 650 && l3[2] == 600);                   // Office Reef full; Ultimate MB 80 % x 0.75
    // a picture mode for Ultimate leaves Office Reef on its schedule
    o3.mode = MODE_PHOTO; o3.until = 5000; for (uint8_t g = 0; g < MAX_GROUPS; g++) o3.lvl[g] = d.photo[3].lvl[g];
    groupLevels(d, o3, true, 1000, 15 * 3600, l3); CHECK(l3[0] == 650 && l3[2] == 800);
    // an output with its own times and intensity (Now › Edit): OUT 1 blue 12:00-16:00 at 87 %
    d.aq[0].adj[0] = {1, 12 * 60, 16 * 60, {87, 79}}; sanitize(d);
    CHECK(d.nGroups == 7 && d.ch[0].group == 2 && d.ch[1].group == 3 && groupInt(d, 2) == 87 && !strcmp(d.group[2].name, "Office Reef: OUT 1 Blue") && d.ch[2].group == 5);
    CHECK(curveAt(d.group[2], 11 * 3600) == 0 && curveAt(d.group[2], 14 * 3600) == 1000 && d.group[2].p[d.group[2].n - 1].min == 16 * 60); }
  // ---- custom is expert mode; an old config without a setup is described from its jacks
  { Config x = c; x.setup.kind = SK_CUSTOM; sanitize(x); CHECK(x.setup.kind == SK_ALTAIR);
    Config m = c; m.jack[0].fixture = FX_ALTAIR_100; m.jack[0].count = 5; setupFromJacks(m);
    CHECK(m.setup.model == FX_ALTAIR_100 && m.setup.qty == 5 && !m.setup.par); }
  // mapCurve: points stretched over the photoperiod, wrapping past midnight (on 18:00, 10 h)
  { const MapPt mp[] = {{0, 0}, {500, 1000}, {1000, 0}}; Group n{}; mapCurve(n, mp, 3, 1080, 600);
    CHECK(curveAt(n, 23 * 3600) == 1000 && curveAt(n, 12 * 3600) == 0 && curveAt(n, 2 * 3600) == 400);
    for (uint8_t i = 1; i < n.n; i++) CHECK(n.p[i - 1].min <= n.p[i].min); }

  // interpolation
  Group g{}; g.n = 2; g.p[0] = {360, 1000}; g.p[1] = {1080, 200};
  CHECK(curveAt(g, 0) == 600);                             // midnight halfway 18:00 -> 06:00
  CHECK(curveAt(g, 720 * 60) == 600);

  // ---- acclimation
  c.acclOn = true; c.acclStartPct = 50; c.acclDays = 10; c.acclStart = 1'000'000;
  CHECK(acclFactor(c, 999'000) == 500);
  CHECK(acclFactor(c, 1'000'000 + 5 * DAY) == 750);
  CHECK(acclFactor(c, 1'000'000 + 20 * DAY) == 1000);
  c.acclOn = false; CHECK(acclFactor(c, 0) == 1000);

  // ---- group levels in auto, with intensity and acclimation
  Override o; uint16_t lv[MAX_GROUPS];
  uint32_t noon = 1'700'000'000;                            // any epoch; secOfDay passed separately
  CHECK(groupLevels(c, o, true, noon, 14 * 3600, lv) == MODE_AUTO && lv[0] == 650 && lv[1] == 650);   // 100 % x 65 %
  c.intensity = 50; aqPush(c); groupLevels(c, o, true, noon, 15 * 3600, lv); CHECK(lv[0] == 500);
  c.acclOn = true; c.acclStart = noon; c.acclStartPct = 50; aqPush(c); groupLevels(c, o, true, noon, 15 * 3600, lv); CHECK(lv[0] == 250);
  c.acclOn = false; c.intensity = 100; aqPush(c);
  groupLevels(c, o, false, 0, 0, lv); CHECK(lv[0] == NO_CLOCK_LEVEL && lv[1] == NO_CLOCK_LEVEL && lv[2] == 0);

  // ---- manual and picture overrides
  o.mode = MODE_MANUAL; o.until = 0; o.lvl[0] = 300; o.lvl[1] = 0;
  CHECK(!overrideActive(o, true, noon));                    // no "until I resume" any more
  CHECK(overrideMinutes(0) == 30 && overrideMinutes(45) == 60 && overrideMinutes(1440) == 60);
  o.until = noon + 3600;
  CHECK(groupLevels(c, o, true, noon, 14 * 3600, lv) == MODE_MANUAL && lv[0] == 300);
  CHECK(overrideActive(o, true, noon) && !overrideActive(o, true, noon + 3600));
  CHECK(groupLevels(c, o, true, noon + 7200, 14 * 3600, lv) == MODE_AUTO);
  CHECK(overrideActive(o, false, 0));                       // clock unknown: keep the user's choice
  o.mode = MODE_PHOTO; o.until = 0; CHECK(!overrideActive(o, true, noon));   // picture mode needs an end
  o = Override{};

  // ---- channels: fixtures, rings, groups, trim, floor
  setFixture(c, 0, FX_ALTAIR_210); setFixture(c, 1, FX_ALTAIR_100);
  setFixture(c, 2, FX_ALTAIR_210); setFixture(c, 3, FX_NONE);
  CHECK(c.ch[0].group == 0 && c.ch[1].group == 1);          // tip -> Blue, ring -> White
  CHECK(c.ch[4].group == 0 && c.ch[5].group == 1);
  CHECK(c.ch[6].group == -1 && c.ch[7].group == -1);        // empty jack
  CHECK(c.jack[0].watts[0] == 150 && c.jack[0].watts[1] == 60 && c.jack[1].watts[0] == 70 && c.jack[1].watts[1] == 30);
  { Config h = c; h.expert = true; setFixture(h, 3, FX_HELIX_RCD, 3);              // expert mode: outputs set by hand
    CHECK(h.ch[6].group == GG_WHITE && h.ch[7].group == -1 && h.jack[3].watts[0] == 92 && h.jack[3].watts[1] == 0);
    CHECK(channelInUse(h, 6) && !channelInUse(h, 7));        // ring held at 0 V
    setFixture(h, 3, FX_ATLAS); CHECK(h.jack[3].watts[0] == 240 && h.ch[6].group == GG_WHITE && h.jack[3].len == 0);
    setFixture(h, 3, FX_HELIX_MB); CHECK(h.ch[6].group == GG_BLUE);
    h.jack[3].watts[0] = 999; CHECK(sanitize(h) == 1 && h.jack[3].watts[0] == fixtureWatts(FX_HELIX_MB, 0, 0));   // Quanta watts are locked
    h.ch[6].group = GG_WHITE; sanitize(h); CHECK(h.ch[6].group == GG_WHITE);  // expert may regroup
    setFixture(h, 3, FX_OTHER); CHECK(!channelInUse(h, 6) && !channelInUse(h, 7));
    h.ch[7].group = GG_WHITE; h.jack[3].watts[1] = 45; CHECK(sanitize(h) == 0 && channelInUse(h, 7) && !channelInUse(h, 6)); }
  CHECK(channelInUse(c, 0) && channelInUse(c, 1) && channelInUse(c, 5) && !channelInUse(c, 6) && !channelInUse(c, 7));
  uint16_t grp[MAX_GROUPS] = {800, 400, 0, 0, 0, 0};
  CHECK(channelLevel(c, grp, 0) == 800);
  CHECK(channelLevel(c, grp, 1) == 400);
  c.ch[7].group = 0; CHECK(channelLevel(c, grp, 7) == 0); c.ch[7].group = -1;   // empty jack stays at 0 V
  CHECK(channelLevel(c, grp, 5) == 400);
  c.ch[4].trim = 50; CHECK(channelLevel(c, grp, 4) == 400);
  grp[0] = 30; c.floorPct = 10; CHECK(channelLevel(c, grp, 0) == 100);   // 3 % raised to the 10 % floor
  grp[0] = 0;  CHECK(channelLevel(c, grp, 0) == 0);                      // but 0 stays off
  CHECK(channelLevel(c, grp, 6) == 0 && channelLevel(c, grp, 99) == 0);

  // ---- DAC codes and calibration
  CHECK(levelToCode(0, 100) == 0);
  CHECK(levelToCode(1000, 100) == 4081);                    // 10.00 V of a 10.035 V full scale
  CHECK(levelToCode(1000, 120) == 4095);                    // clamps
  CHECK(levelToCode(500, 100) == 2040);
  uint16_t mv = codeToMilliVolts(levelToCode(1000, 100)); CHECK(mv >= 9995 && mv <= 10005);

  // ---- jack voltage with calibration, monitor zeroing
  CHECK(jackMilliVolts(levelToCode(500, 102), 102) >= 4995 && jackMilliVolts(levelToCode(500, 102), 102) <= 5005);
  CHECK(monitorGainFor(5000, 4925) == 1015 && monitorGainFor(5000, 4000) == 0 && monitorGainFor(1000, 1000) == 0);
  CHECK(applyMonitorGain(4925, 1015) == 4999 && applyMonitorGain(-1, 1015) == -1 && applyMonitorGain(5000, 0) == 5000);
  { // driver pushing 200 uA in: op-amp side sits 200 mV under the jack (4800 mV); a divider 1.5 % low reads 4729
    int amp = applyMonitorGain(4729, 1015);
    OutReading r = classifyOutput(5000, (uint16_t)amp, true); CHECK(r.status == OUT_OK && r.microAmps >= 195 && r.microAmps <= 205);
    OutReading u = classifyOutput(5000, 4729, true); CHECK(u.microAmps > 250); }   // uncorrected: ~270 uA, way off

  // ---- slew
  CHECK(slew(0, 1000, 1000, 2000) == 500);
  CHECK(slew(900, 1000, 1000, 2000) == 1000);
  CHECK(slew(1000, 0, 100, 2000) == 950);
  CHECK(slew(5, 7, 1, 1'000'000) == 6);                     // always makes progress
  CHECK(slew(10, 20, 5, 0) == 20);

  // ---- output monitoring
  CHECK(classifyOutput(5000, 4800, true).status == OUT_OK);          // 200 uA = one or two drivers
  CHECK(classifyOutput(5000, 4800, true).microAmps == 200);
  CHECK(classifyOutput(5000, 4995, true).status == OUT_NO_LOAD);
  CHECK(classifyOutput(5000, 13500, true).status == OUT_SHORT);      // op-amp at rail, jack dead short
  CHECK(classifyOutput(9800, 9790, true).status == OUT_UNKNOWN);     // too close to driver's own voltage
  CHECK(classifyOutput(0, 0, false).status == OUT_IDLE);

  // ---- sanitize fixes garbage (e.g. a hand-edited backup)
  Config bad; memset(&bad, 0xFF, sizeof(bad)); bad.expert = false; bad.acclOn = false; bad.setupDone = false; bad.setup.par = true;
  int fixed = sanitize(bad);
  CHECK(fixed > 10);
  CHECK(bad.intensity == 100 && bad.floorPct == 40 && bad.tank == TANK_MIXED && bad.photo[0].minutes == 30);
  for (uint8_t i = 0; i < CHANNELS; i++) CHECK(bad.ch[i].group < (int8_t)bad.nGroups && bad.ch[i].cal <= 120);
  for (uint8_t gg = 0; gg < MAX_GROUPS; gg++) CHECK(bad.group[gg].n <= MAX_POINTS && bad.group[gg].name[GNAME_LEN - 1] == 0);
  CHECK(sanitize(bad) == 0);                                // second pass finds nothing
  uint16_t lv2[MAX_GROUPS];
  groupLevels(bad, Override{}, true, noon, 12 * 3600, lv2);  // runs without faulting
  for (auto v : lv2) CHECK(v <= LEVEL_MAX);

  // ---- ids and crc
  CHECK(fixtureFromId("altair_210") == FX_ALTAIR_210 && fixtureFromId("helix_mb") == FX_HELIX_MB && fixtureFromId("other") == FX_OTHER && fixtureFromId("bogus") == FX_NONE);
  CHECK(crc32((const uint8_t *)"123456789", 9) == 0xCBF43926u);

  printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
  return fails ? 1 : 0;
}
