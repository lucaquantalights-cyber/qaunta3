#!/usr/bin/env python3
"""
Quanta Manufacture Device  (same flow as the AIDoser manufacture_device.py)

  1. Upload Quanta Test (test/burn-in firmware) and read the board's identity
  2. Enroll the unit in scripts/devices.json
  3. Full hardware test (every chip, all 8 outputs, 15 V supply, battery clock)
  4. Operator checks: status LED colours, SW1 and BOOT buttons
  5. Set the battery clock from this PC, then (optional) power-cut test of the CR2032
  6. Burn-in
  7. Claim code from claim_inventory + Firebase (device login, claim records)
  8. Write the cloud identity into the board's memory (kept when the final firmware is flashed)
  9. Upload the final quanta3 firmware and confirm ID, firmware version and MAC
 10. Label file + manufacturing report

Run from the quanta3 folder:   python scripts\\manufacture_quanta.py

PRACTICE MODE (answer "n" to "Is this a real Quanta board?"): for an ESP32 dev board before the real
boards exist. Uploads Quanta Test, reads the board, shows the test results without stopping on them,
tests the BOOT button, writes a practice ID, uploads quanta3 and checks it. No claim codes, no Firebase.
Quanta Test project:           ..\\quanta-test  (next to quanta3)  or  quanta3\\quanta-test
"""

import json
import re
import secrets
import shutil
import string
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

import serial
import serial.tools.list_ports

# ----------------------------------------------------------------------------- settings
PROJECT_DIR = Path(__file__).resolve().parents[1]                 # quanta3 (final firmware)
# Quanta Test ships INSIDE this project (qaunta3\\quanta-test), so one folder has everything.
TEST_DIR_CANDIDATES = [PROJECT_DIR / "quanta-test", PROJECT_DIR.parent / "quanta-test", PROJECT_DIR.parent / "quantatest"]
TEST_ENV = "esp32dev"
FINAL_ENV = "esp32dev"
BOARD = "quanta-4ch-v1"
DEVICES_FILE = PROJECT_DIR / "scripts" / "devices.json"
CLOUD_FILE = PROJECT_DIR / "scripts" / "quanta_cloud.json"
FIRMWARE_SRC = PROJECT_DIR / "src" / "main.cpp"                   # FW_VERSION is read from here
PIO_EXE = Path.home() / ".platformio" / "penv" / "Scripts" / "platformio.exe"   # works for any Windows user
REPORTS_DIR = PROJECT_DIR / "manufacturing_reports"
LABELS_DIR = PROJECT_DIR / "labels"
CLAIM_INVENTORY_FILE = PROJECT_DIR / "claim_inventory" / "claim_inventory.json"
SERVICE_ACCOUNT_FILE = PROJECT_DIR / "secrets" / "quanta-firebase-service-account.json"
BAUD = 115200
DEVICE_PREFIX = "quanta"
LABEL_PREFIX = "QNT-"

CLOUD_DEFAULTS = {
    "databaseUrl": "https://quanta-f9c4c-default-rtdb.firebaseio.com",
    "apiKey": "",                         # Firebase Web API key (Project settings > General). Not a secret.
    "deviceEmailDomain": "device.aidoser.tech",
    "setupUrl": "",                       # printed on the label if set
}

# Tests that must pass. Outputs may also be CHECK when the op-amp hit its limit (LED test load pulls a lot).
REQUIRED = ["pullups", "bus1", "bus2", "dacA", "dacB", "adc",
            "out1", "out2", "out3", "out4", "out5", "out6", "out7", "out8", "supply", "rtc"]

# Outputs whose "light detected" current reading is skipped: they pass on the operator's visual check alone.
# This board batch can't sense the light on jack 1 (OUT1/OUT2) although the outputs work.
# Empty this list ( [] ) for the new boards.
SKIP_SENSING = ["out1", "out2"]

LOG = []                                  # every line from the board, for the report
PRACTICE = False                          # ESP32 dev board instead of a Quanta board: practice the flow, no cloud


# ----------------------------------------------------------------------------- helpers
def fail(msg):
    print(f"\nERROR: {msg}")
    print("\nUNIT NOT FINISHED - set it aside and fix the problem above.")
    sys.exit(1)


def now_iso():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def ask_yes_no(prompt, default=False):
    suffix = " [Y/n]: " if default else " [y/N]: "
    while True:
        answer = input(prompt + suffix).strip().lower()
        if not answer:
            return default
        if answer in ("y", "yes"):
            return True
        if answer in ("n", "no"):
            return False


def prompt(label, default):
    return input(f"{label} [{default}]: ").strip() or default


def banner(text):
    print("\n" + "=" * 56)
    print(f"  {text}")
    print("=" * 56)


def pio_exe():
    if PIO_EXE.exists():
        return str(PIO_EXE)
    for name in ("pio", "platformio"):
        found = shutil.which(name)
        if found:
            return found
    fail(f"PlatformIO not found at {PIO_EXE} or on PATH.")


def test_dir(report=False):
    for d in TEST_DIR_CANDIDATES:
        if (d / "platformio.ini").exists():
            src = d / "src" / "main.cpp"
            m = re.search(r'#define\s+TEST_VERSION\s+"QT-(\d+)\.(\d+)"',
                          src.read_text(encoding="utf-8", errors="ignore") if src.exists() else "")
            ver = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
            if report:
                print(f"Quanta Test folder: {d}  (QT-{ver[0]}.{ver[1]})")
            if ver < (1, 5):
                fail(f"The Quanta Test in this folder is too old (QT-{ver[0]}.{ver[1]}):\n  {d}\n"
                     "Replace that folder with the quanta-test folder from quanta-manufacturing.zip (QT-1.5 or newer).")
            return d
    fail("Quanta Test project not found. Put the quanta-test folder next to quanta3.\nLooked in:\n  "
         + "\n  ".join(map(str, TEST_DIR_CANDIDATES)))


def final_fw_version():
    if not FIRMWARE_SRC.exists():
        fail(f"quanta3 firmware source missing:\n{FIRMWARE_SRC}")
    m = re.search(r'#define\s+FW_VERSION\s+"([^"]+)"', FIRMWARE_SRC.read_text(encoding="utf-8", errors="ignore"))
    if not m:
        fail("FW_VERSION not found in src/main.cpp")
    return m.group(1)


NOT_A_BOARD = ("active management", "sol)", "bluetooth", "modem")   # the computer's own ports: never a Quanta board


def choose_port(previous=None):
    while True:
        allp = list(serial.tools.list_ports.comports())
        ports = [p for p in allp if not any(w in (p.description or "").lower() for w in NOT_A_BOARD)]
        if ports:
            break
        print("\nNo programmer found. Ports seen:" + ("".join(f"\n  {p.device}  {p.description}" for p in allp) or " none"))
        print("Plug the ESP32 loader's USB cable into this computer (and check its light is on).")
        if input("Press Enter to look again, or type q to quit: ").strip().lower() == "q":
            fail("No programmer connected.")
    if previous and any(p.device == previous for p in ports):
        return previous
    if len(ports) == 1:
        print(f"\nUsing {ports[0].device}  {ports[0].description}")
        return ports[0].device
    print("\nAvailable ports:")
    for i, port in enumerate(ports, 1):
        print(f"  {i}. {port.device}  {port.description}")
    while True:
        try:
            choice = int(input("Select port: ").strip())
            if 1 <= choice <= len(ports):
                return ports[choice - 1].device
        except ValueError:
            pass


def run(cmd, cwd, description):
    print(f"\n{description}")
    print(" ".join(map(str, cmd)))
    result = subprocess.run(cmd, cwd=cwd)
    if result.returncode != 0:
        fail(f"{description} failed.")


# ----------------------------------------------------------------------------- serial
def open_port(port, settle=2.5):
    """Open the port WITHOUT touching the reset lines, then give the ESP32 a clean normal restart.
    (On dev boards, DTR/RTS drive BOOT and EN. Windows can otherwise leave the chip waiting in upload mode.)"""
    last = None
    for _ in range(15):
        try:
            ser = serial.Serial()
            ser.port, ser.baudrate, ser.timeout = port, BAUD, 1
            ser.dtr = False                       # BOOT/IO0 not held
            ser.rts = False                       # EN not held
            ser.open()
            ser.dtr = False
            ser.rts = True                        # EN low: reset
            time.sleep(0.15)
            ser.rts = False                       # EN high, IO0 high: start the program normally
            time.sleep(settle)
            return ser
        except Exception as exc:
            last = exc
            time.sleep(1)
    fail(f"Could not open {port}: {last}")


def send(ser, text):
    ser.write((text + "\n").encode())
    ser.flush()


def read_until(ser, done, timeout, echo=True):
    """Read lines until done(line) is True. Returns that line, or None on timeout."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode(errors="ignore").strip()
        if not line:
            continue
        if echo:
            print(line)
        LOG.append(line)
        if done(line):
            return line
    return None


def wait_test_ready(ser, timeout=180):
    """Quanta Test runs its start-up tests (~20-60 s), then prints QT_READY.
    If the port didn't reset the board, it's already running: IDENTITY gets an answer."""
    print("\nWaiting for Quanta Test to finish its start-up tests...")
    start = time.time()
    last_poke = 0
    while time.time() - start < timeout:
        if time.time() - last_poke > 15:
            send(ser, "IDENTITY")
            last_poke = time.time()
        line = read_until(ser, lambda l: l in ("QT_READY", "IDENTITY_END") or "QT-1.0" in l
                          or l.startswith("Type a letter and press Enter"), 5)
        if line and ("QT-1.0" in line or line.startswith("Type a letter")):
            # QT-1.0 has no factory commands: IDENTITY just brings the menu back
            send(ser, "IDENTITY")
            if not read_until(ser, lambda l: l == "IDENTITY_END", 8):
                fail("The board is running an OLD Quanta Test (QT-1.0) without the factory commands.\n"
                     "Replace the quanta-test folder with the one from quanta-manufacturing.zip (QT-1.1 or newer).")
            return
        if line:
            read_until(ser, lambda l: False, 1.5)          # let any queued answer finish
            return
    fail("Quanta Test did not answer. Is it uploaded, and is the serial cable connected?")


def results_from(lines):
    res = {}
    for line in lines:
        m = re.match(r"RESULT (\S+) (PASS|FAIL|CHECK|NOTRUN)\s?(.*)", line)
        if m:
            res[m.group(1)] = (m.group(2), m.group(3))
    return res


# ----------------------------------------------------------------------------- steps
def upload_test(port):
    run([pio_exe(), "run", "--project-dir", str(test_dir()), "-e", TEST_ENV, "-t", "upload", "--upload-port", port],
        PROJECT_DIR, "Building and uploading Quanta Test...")


def read_identity(ser):
    send(ser, "IDENTITY")
    mac = chip = hotspot = None
    board = BOARD
    deadline = time.time() + 20
    while time.time() < deadline:
        line = read_until(ser, lambda l: True, 2)
        if not line:
            continue
        m = re.search(r"MAC Address:\s*([0-9A-Fa-f:]+)", line)
        if m:
            mac = m.group(1).upper()
        m = re.search(r"Chip ID:\s*([0-9A-Fa-f]+)", line)
        if m:
            chip = m.group(1).upper()
        m = re.search(r"Board:\s*(.+)", line)
        if m:
            board = m.group(1).strip()
        m = re.search(r"Hotspot:\s*(\S+)", line)
        if m:
            hotspot = m.group(1)
        if line == "IDENTITY_END" and mac and chip:
            return {"macAddress": mac, "chipId": chip, "board": board, "hotspot": hotspot or ""}
    fail("Quanta Test did not report the board's identity.")


def load_devices():
    return json.loads(DEVICES_FILE.read_text(encoding="utf-8")) if DEVICES_FILE.exists() else {}


def save_devices(data):
    DEVICES_FILE.parent.mkdir(parents=True, exist_ok=True)
    if DEVICES_FILE.exists():
        DEVICES_FILE.with_suffix(".json.bak").write_bytes(DEVICES_FILE.read_bytes())
    DEVICES_FILE.write_text(json.dumps(data, indent=4) + "\n", encoding="utf-8")


def update_device(device_id, **fields):
    db = load_devices()
    db[device_id].update(fields)
    save_devices(db)
    return db[device_id]


def prefix():
    return "practice" if PRACTICE else DEVICE_PREFIX


def label_prefix():
    return "PRACTICE-" if PRACTICE else LABEL_PREFIX


def next_free_number(db):
    nums = [int(m.group(1)) for k in db for m in [re.match(rf"{prefix()}(\d+)$", k)] if m]
    return max(nums, default=0) + 1


def enroll(identity, version):
    db = load_devices()
    for k, v in db.items():
        if str(v.get("macAddress", "")).upper() == identity["macAddress"]:
            print(f"\nThis board is already in devices.json as {k} (status: {v.get('manufactureStatus', '?')}).")
            if ask_yes_no(f"Re-run manufacturing for {k}?", True):
                default_id = k
                break
    else:
        default_id = f"{prefix()}{next_free_number(db)}"
    n = re.sub(r"\D", "", default_id) or "1"
    device_id = prompt("Device ID", default_id)
    customer = prompt("Customer name", db.get(device_id, {}).get("customer", "Burn-In Pending"))
    label = prompt("Label", db.get(device_id, {}).get("label", f"{label_prefix()}{int(n):05d}"))
    if device_id in db and str(db[device_id].get("macAddress", "")).upper() not in ("", identity["macAddress"]):
        if not ask_yes_no(f"{device_id} exists with a DIFFERENT board (MAC {db[device_id]['macAddress']}). Overwrite?", False):
            fail("Cancelled.")
    entry = db.get(device_id, {})
    entry.update({
        "customer": customer,
        "label": label,
        "deviceId": device_id,
        "firmwareVersion": version,
        "chipId": identity["chipId"],
        "macAddress": identity["macAddress"],
        "board": identity["board"],
        "hotspot": identity["hotspot"],
        "manufactureStatus": "TESTING",
        "practice": PRACTICE,
        "manufactureStartedAt": datetime.now().isoformat(timespec="seconds"),
    })
    db[device_id] = entry
    save_devices(db)
    return entry


def hardware_test(ser, device):
    banner("HARDWARE TEST")
    if PRACTICE:
        print("PRACTICE: a dev board has none of the Quanta chips, so most tests will FAIL. That's expected.")
    else:
        print("15 V adapter plugged in?  UNPLUG every light and LED from the 4 jacks for this part.")
        print("(The outputs are first measured with nothing connected; the light is tested jack by jack after.)")
    input("Press Enter to start...")
    start = len(LOG)
    send(ser, "AUTO")
    end = read_until(ser, lambda l: l.startswith("TEST_COMPLETE"), 300)
    if not end:
        fail("Hardware test did not finish (no TEST_COMPLETE).")
    res = results_from(LOG[start:])
    problems = []
    for code in REQUIRED:
        word, note = res.get(code, ("NOTRUN", "no result"))
        if word == "PASS":
            continue
        if word == "CHECK" and code.startswith("out") and ("limit" in note or "no load current" in note
                                                           or "no light detected" in note):
            continue          # nothing plugged in (expected here) or LED test load at its limit: the light test follows
        if word == "CHECK" and code == "rtc" and "time not set" in note:
            continue          # new clock chip: it's ticking; the script sets its time a few steps later
        problems.append(f"{code}: {word} {note}")
    update_device(device["deviceId"], hardwareTest="FAIL" if problems else "PASS",
                  hardwareTestAt=datetime.now().isoformat(timespec="seconds"),
                  hardwareResults={k: {"result": v[0], "note": v[1]} for k, v in res.items()})
    if PRACTICE:
        update_device(device["deviceId"], hardwareTest="PRACTICE")
        print(f"\nPRACTICE: {len(problems)} tests would stop a real unit (expected on a dev board). Carrying on.")
        return
    if problems:
        print("\nFAILED TESTS:")
        for p in problems:
            print("  " + p)
        update_device(device["deviceId"], manufactureStatus="FAILED_TEST")
        fail("Hardware test failed.")
    print("\nHardware test: PASS")


def zero_outputs(ser, device):
    """Measure all 8 outputs with nothing plugged in, so a real light's small current can be detected after."""
    start = len(LOG)
    send(ser, "ZERO")
    line = read_until(ser, lambda l: l.startswith("ZERO_DONE"), 60)
    m = re.search(r"ok=(\d+)", line or "")
    if not m:
        fail("Output zeroing did not finish.")
    if int(m.group(1)) != 8:
        bad = [l for l in LOG[start:] if l.startswith("ZERO") and "gain=0" in l]
        update_device(device["deviceId"], manufactureStatus="FAILED_TEST")
        fail("Some outputs measure more than 10 % off with nothing plugged in:\n  " + "\n  ".join(bad)
             + "\nIs something still plugged into those jacks? Unplug everything and run again.")
    print("Outputs zeroed (all 8).")


def choose_load():
    print("\nWhat will you test the outputs with?")
    print("  1. A real light (one two-channel light, moved from jack to jack)")
    print("  2. The LED test board (plugged into all 4 jacks at once)")
    while True:
        a = input("Choose 1 or 2 [1]: ").strip() or "1"
        if a in ("1", "2"):
            return "light" if a == "1" else "ledboard"


def light_test(ser, device):
    """Tests tip (first channel) and ring (second) of every jack, with a real light moved jack to jack
    or with the LED test board in all 4 jacks."""
    load = choose_load()
    banner("OUTPUT TEST, JACK BY JACK" if load == "light" else "OUTPUT TEST WITH THE LED TEST BOARD")
    if load == "light":
        print("You'll move the light from jack to jack. For each jack the board ramps both channels,")
        print("measures them, then turns the first channel (tip) fully on for 2 s, then the second channel (ring).")
    else:
        input("\nPlug the LED test board into ALL 4 jacks and press Enter...")
        print("It runs through all 4 jacks by itself: in each jack both LEDs ramp up, then the tip LED is fully on")
        print("for 2 s, then the ring LED. Just watch; it asks one question at the end.")
    what = "light's FIRST channel come on, then the SECOND" if load == "light" else "TIP LED light, then the RING LED"
    problems = []
    results = {}
    for j in range(1, 5):
        a, b = 2 * j - 1, 2 * j
        if load == "light":
            input(f"\nPlug the light into JACK {j} (OUT{a} + OUT{b}) and press Enter...")
        else:
            print(f"\nJACK {j} (OUT{a} + OUT{b}): watch its two LEDs")
        for attempt in range(1, 4):                     # up to 3 tries: a plug not fully home is the usual cause
            start = len(LOG)
            send(ser, f"JACKTEST {j}")
            if not read_until(ser, lambda l: l.startswith("JACKTEST_"), 60):
                fail(f"Jack {j} test did not finish.")
            res = results_from(LOG[start:])
            bad = [o for o in (a, b) if res.get(f"out{o}", ("NOTRUN",))[0] != "PASS"
                   and not (f"out{o}" in SKIP_SENSING and "no light detected" in res.get(f"out{o}", ("", ""))[1])]
            if not bad or attempt == 3:
                break
            names = " and ".join(f"OUT{o} ({'tip' if o == a else 'ring'})" for o in bad)
            print(f"\n{names} on jack {j}: {'light' if load == 'light' else 'LED'} not detected (try {attempt} of 3).")
            ans = input(f"Push the plug FULLY into jack {j}, then press Enter to test it again (or type s to stop): ")
            if ans.strip().lower() == "s":
                break
        for o in (a, b):
            word, note = res.get(f"out{o}", ("NOTRUN", "no result"))
            if f"out{o}" in SKIP_SENSING and word == "CHECK" and "no light detected" in note:
                word, note = "PASS", "sensing skipped on this board batch; passed on visual check"
                print(f"OUT{o}: light sensing skipped (SKIP_SENSING) - your visual check decides.")
            results[f"out{o}"] = {"result": word, "note": note}
            if word != "PASS":
                problems.append(f"OUT{o} (jack {j} {'tip' if o == a else 'ring'}): {word} {note}")
        if load == "light" and not ask_yes_no(f"On jack {j}, did the {what}?", True):
            problems.append(f"Jack {j}: did not light as expected (operator)")
    if load == "ledboard" and not ask_yes_no("\nDid all 8 LEDs light, jack 1 to 4, tip then ring in each?", True):
        problems.append("Not every LED lit in the right order (operator). Watch again by hand: Serial Monitor, type v")
    update_device(device["deviceId"], outputLoad=load, lightTest="FAIL" if problems else "PASS", lightResults=results)
    if problems:
        print("\nOUTPUT TEST FAILED:")
        for p in problems:
            print("  " + p)
        update_device(device["deviceId"], manufactureStatus="FAILED_TEST")
        fail("Output test failed.")
    print("\nAll 4 jacks / 8 outputs: PASS")


def operator_checks(ser, device):
    banner("STATUS LED AND BUTTONS")
    if PRACTICE:
        return practice_button(ser, device)
    print("Watch the status LED: it shows red, green, blue, white (twice).")
    send(ser, "LED")
    if not read_until(ser, lambda l: l == "LED_DONE", 20):
        fail("LED test did not finish.")
    led_ok = ask_yes_no("Did the LED show red, green, blue and white?", False)
    if not led_ok:
        update_device(device["deviceId"], ledTest="FAIL", manufactureStatus="FAILED_TEST")
        fail("Status LED failed (check U11, its 5 V, and IO26 to DIN).")

    print("\nWhen the board says 'Press and release SW1 now', press and release SW1 on the Quanta board.")
    start = len(LOG)
    send(ser, "BUTTONS SW1")
    if not read_until(ser, lambda l: l == "BUTTONS_DONE", 45):
        fail("Button test did not finish.")
    res = results_from(LOG[start:])
    bad = [f"{k}: {res.get(k, ('NOTRUN', ''))[0]} {res.get(k, ('', ''))[1]}" for k in ("sw1",)
           if res.get(k, ("NOTRUN",))[0] != "PASS"]
    update_device(device["deviceId"], ledTest="PASS", buttonTest="FAIL" if bad else "PASS")
    if bad:
        update_device(device["deviceId"], manufactureStatus="FAILED_TEST")
        fail("Button test failed:\n  " + "\n  ".join(bad))
    print("\nLED and buttons: PASS")


BENCH_PINS = [13, 14, 18, 19, 23, 25, 32, 33]   # PWM_PINS in config.h: OUT1..OUT8 on a dev board


def practice_leds(ser, device):
    banner("LED CHECK (dev board)")
    if not ask_yes_no("Are your 8 LEDs connected to the dev board?", True):
        update_device(device["deviceId"], ledWalk="SKIPPED")
        return
    print("Each LED fades up, stays on 1.5 s, and fades down. Answer after each one.")
    dead = []
    for n in range(1, 9):
        print(f"\nOUT{n}  (ESP32 pin {BENCH_PINS[n - 1]})")
        send(ser, f"OUTWALK {n}")
        line = read_until(ser, lambda l: l.startswith("OUTWALK_"), 15, echo=False)
        if not line or not line.startswith("OUTWALK_DONE"):
            fail(f"The board couldn't drive OUT{n} (is it Quanta Test QT-1.3 or newer?).")
        if not ask_yes_no(f"Did OUT{n} light up?", True):
            dead.append(f"OUT{n} (pin {BENCH_PINS[n - 1]})")
    update_device(device["deviceId"], ledWalk="PASS" if not dead else "FAIL: " + ", ".join(dead))
    if dead:
        print("\nThese LEDs did not light: " + ", ".join(dead))
        print("Check the LED is the right way round (long leg toward the pin), its resistor, and the wire to that pin.")
        if not ask_yes_no("Carry on with the rest of the practice run anyway?", False):
            fail("LED check failed.")
    else:
        print("\nAll 8 LEDs: PASS")


def practice_button(ser, device):
    print("PRACTICE: a dev board has no status LED or SW1, so this tests its BOOT button instead.")
    print("When it says 'Press and release BOOT now', press and release the RIGHT button (BOOT).")
    start = len(LOG)
    send(ser, "BUTTONS BOOT")
    if not read_until(ser, lambda l: l == "BUTTONS_DONE", 45):
        fail("Button test did not finish.")
    word = results_from(LOG[start:]).get("boot", ("NOTRUN", ""))[0]
    update_device(device["deviceId"], buttonTest=f"PRACTICE-BOOT-{word}")
    if word != "PASS":
        fail("BOOT button press not seen. Press the RIGHT button when the board asks.")
    print("\nBOOT button: PASS")


def set_clock(ser):
    send(ser, f"SETTIME {int(time.time())}")
    line = read_until(ser, lambda l: l.startswith("SETTIME_"), 10)
    if not line or not line.startswith("SETTIME_OK"):
        fail("Could not set the battery clock.")
    print("Battery clock set from this PC.")


def battery_test(ser, port, device):
    banner("CLOCK BATTERY TEST")
    if not ask_yes_no("Run the power-cut test of the CR2032 now?", True):
        update_device(device["deviceId"], batteryTest="SKIPPED")
        return ser, port
    ser.close()
    print("\n1. Unplug ALL power: the 15 V adapter AND the USB/programming cable.")
    print("2. Wait 30 seconds.")
    print("3. Plug both back in.")
    input("Press Enter when it's plugged back in...")
    port = choose_port(port)
    ser = open_port(port)
    wait_test_ready(ser)
    send(ser, "RTCSTATUS")
    line = read_until(ser, lambda l: l.startswith("RTCSTATUS"), 10)
    m = re.search(r"ok=(\d) epoch=(\d+) osfAtBoot=(\d)", line or "")
    if not m:
        fail("Clock status not reported.")
    ok, epoch, osf = int(m.group(1)), int(m.group(2)), int(m.group(3))
    drift = epoch - int(time.time())
    if not ok or osf or epoch == 0 or abs(drift) > 5:
        update_device(device["deviceId"], batteryTest="FAIL", manufactureStatus="FAILED_TEST")
        fail(f"The clock did not keep time while unplugged (ok={ok}, stopped={osf}, off by {drift} s).\n"
             "Check the CR2032 is fitted + side up and BT1 is soldered.")
    update_device(device["deviceId"], batteryTest="PASS", batteryDriftSec=drift)
    print(f"\nClock battery: PASS (kept time, off by {drift} s)")
    return ser, port


def burn_in(ser, device):
    banner("BURN-IN")
    minutes = int(prompt("Burn-in duration minutes", "10"))
    update_device(device["deviceId"], burnInStatus="RUNNING", burnInStartedAt=datetime.now().isoformat(timespec="seconds"))
    send(ser, f"BURNIN {minutes}")
    print("Burn-in running. Ctrl+C stops safely.")
    result = "FAIL"
    try:
        line = read_until(ser, lambda l: l.startswith("BURNIN_COMPLETE"), minutes * 60 + 180)
        if line and "result=PASS" in line:
            result = "PASS"
    except KeyboardInterrupt:
        send(ser, "STOP")
        read_until(ser, lambda l: l.startswith("BURNIN_COMPLETE"), 30)
    update_device(device["deviceId"], burnInStatus=result, burnInCompletedAt=datetime.now().isoformat(timespec="seconds"))
    if result != "PASS":
        update_device(device["deviceId"], manufactureStatus="FAILED_BURNIN")
        fail("Burn-in did not pass.")
    print("\nBurn-in: PASS")


# ----------------------------------------------------------------------------- cloud
def load_cloud():
    cfg = dict(CLOUD_DEFAULTS)
    if CLOUD_FILE.exists():
        cfg.update(json.loads(CLOUD_FILE.read_text(encoding="utf-8")))
    cfg["apiKey"] = str(cfg.get("apiKey", "")).strip().strip('"').strip("'").strip()   # pasted with quote marks: remove them
    if not cfg["apiKey"]:
        cfg["apiKey"] = input("Firebase Web API key for the Quanta project (Project settings > General): ").strip()
        cfg["apiKey"] = cfg["apiKey"].strip('"').strip("'").strip()
        if not cfg["apiKey"]:
            fail("The controller needs the Web API key to sign in to the cloud.")
    CLOUD_FILE.write_text(json.dumps(cfg, indent=4) + "\n", encoding="utf-8")
    return cfg


def firebase(cfg):
    try:
        import firebase_admin
        from firebase_admin import auth, credentials, db
    except ImportError:
        fail(f'firebase-admin is not installed. Run:\n"{sys.executable}" -m pip install firebase-admin')
    if not SERVICE_ACCOUNT_FILE.exists():
        fail(f"Quanta Firebase service-account file not found:\n{SERVICE_ACCOUNT_FILE}")
    if not firebase_admin._apps:
        firebase_admin.initialize_app(credentials.Certificate(str(SERVICE_ACCOUNT_FILE)), {"databaseURL": cfg["databaseUrl"]})
    return auth, db


def generate_device_password(length=24):
    alphabet = string.ascii_letters + string.digits + "!@#$%^&*-_"
    while True:
        pw = "".join(secrets.choice(alphabet) for _ in range(length))
        if (any(c.islower() for c in pw) and any(c.isupper() for c in pw)
                and any(c.isdigit() for c in pw) and any(c in "!@#$%^&*-_" for c in pw)):
            return pw


def ensure_device_login(cfg, device):
    auth, _ = firebase(cfg)
    email = device.get("firebaseEmail") or f"{device['deviceId']}@{cfg['deviceEmailDomain']}"
    password = device.get("firebasePassword") or generate_device_password()
    uid = device.get("firebaseUid", "")
    try:
        user = auth.get_user_by_email(email)
        if uid and uid != user.uid:
            fail(f"Firebase UID mismatch for {email}.\ndevices.json: {uid}\nFirebase Auth: {user.uid}")
        auth.update_user(user.uid, password=password, disabled=False)
        print(f"\nFirebase device login verified: {email}  uid {user.uid}")
    except auth.UserNotFoundError:
        user = auth.create_user(email=email, password=password, email_verified=True, disabled=False,
                                display_name=device["deviceId"])
        print(f"\nFirebase device login created: {email}  uid {user.uid}")
    return update_device(device["deviceId"], firebaseEmail=email, firebasePassword=password, firebaseUid=user.uid)


def load_claim_inventory():
    if not CLAIM_INVENTORY_FILE.exists():
        fail("Claim inventory missing. Run: python scripts\\generate_claim_batch.py")
    return json.loads(CLAIM_INVENTORY_FILE.read_text(encoding="utf-8"))


def save_claim_inventory(inventory):
    CLAIM_INVENTORY_FILE.with_suffix(".json.bak").write_bytes(CLAIM_INVENTORY_FILE.read_bytes())
    inventory["updatedAt"] = now_iso()
    CLAIM_INVENTORY_FILE.write_text(json.dumps(inventory, indent=4) + "\n", encoding="utf-8")


def reserve_next_claim(device):
    inventory = load_claim_inventory()
    claims = inventory.get("claims", [])
    for item in claims:
        if item.get("status") == "ASSIGNED" and item.get("deviceId") == device["deviceId"]:
            if (str(item.get("macAddress", "")).upper() != device["macAddress"].upper()
                    or str(item.get("chipId", "")).upper() != device["chipId"].upper()):
                fail(f"Existing claim assignment identity mismatch for {device['deviceId']}.")
            return item["claimCode"], item
    unused = sorted([i for i in claims if i.get("status") == "UNUSED"],
                    key=lambda i: (str(i.get("batch", "")), int(i.get("sequence", 0))))
    if not unused:
        fail("No unused claim codes remain. Generate another batch.")
    item = unused[0]
    item.update({"status": "ASSIGNED", "assignedAt": now_iso(), "deviceId": device["deviceId"], "label": device["label"],
                 "macAddress": device["macAddress"].upper(), "chipId": device["chipId"].upper(), "firebaseStatus": "PENDING"})
    save_claim_inventory(inventory)
    return item["claimCode"], item


def set_claim_firebase_status(claim_code, status):
    inventory = load_claim_inventory()
    for item in inventory.get("claims", []):
        if item.get("claimCode") == claim_code:
            item["firebaseStatus"] = status
            item["firebaseUpdatedAt"] = now_iso()
            save_claim_inventory(inventory)
            return


def bind_claim_in_firebase(cfg, device, claim_code, item):
    _, db = firebase(cfg)
    claim_ref = db.reference(f"/deviceClaims/{device['deviceId']}")
    existing = claim_ref.get()
    if existing and (existing.get("claimed") is True or existing.get("claimedBy")):
        fail(f"{device['deviceId']} is already claimed in Firebase.")
    claim_ref.set({
        "claimCode": claim_code, "claimed": False, "enabled": True, "deviceId": device["deviceId"],
        "label": device["label"], "macAddress": device["macAddress"].upper(), "chipId": device["chipId"].upper(),
        "board": device["board"], "batch": item.get("batch"), "sequence": item.get("sequence"),
        "assignedAt": {".sv": "timestamp"},
    })
    verify = claim_ref.get() or {}
    for field, expected in {"claimCode": claim_code, "deviceId": device["deviceId"],
                            "macAddress": device["macAddress"].upper(), "chipId": device["chipId"].upper()}.items():
        if str(verify.get(field, "")).upper() != str(expected).upper():
            fail(f"Firebase claim verification failed for {field}.")
    db.reference(f"/claimInventory/{claim_code}").set({
        "status": "ASSIGNED", "batch": item.get("batch"), "sequence": item.get("sequence"), "deviceId": device["deviceId"],
        "label": device["label"], "macAddress": device["macAddress"].upper(), "chipId": device["chipId"].upper(),
        "assignedAt": {".sv": "timestamp"},
    })
    db.reference(f"/devices/{device['deviceId']}/info").set({
        "label": device["label"], "macAddress": device["macAddress"].upper(), "chipId": device["chipId"].upper(),
        "board": device["board"], "hotspot": device.get("hotspot", ""), "authUid": device["firebaseUid"],
        "firmwareAtFactory": device["firmwareVersion"], "manufacturedAt": {".sv": "timestamp"},
    })
    set_claim_firebase_status(claim_code, "VERIFIED")


# ----------------------------------------------------------------------------- identity into the board
def provision(ser, cfg, device):
    banner("WRITING CLOUD IDENTITY")
    if PRACTICE:
        send(ser, f"PROV cId {device['deviceId']}")
        line = read_until(ser, lambda l: l.startswith("PROV_"), 10, echo=False)
        if not line or not line.startswith("PROV_OK cId"):
            fail("Could not write the device ID into the board.")
        send(ser, "FACTORY_CLEAN")
        if not read_until(ser, lambda l: l == "CLEAN_OK", 10):
            fail("Could not clear the test settings.")
        print(f"PRACTICE: device ID {device['deviceId']} written (no cloud login).")
        return
    values = {"cId": device["deviceId"], "cEmail": device["firebaseEmail"], "cPass": device["firebasePassword"],
              "cKey": cfg["apiKey"]}
    for key, value in values.items():
        send(ser, f"PROV {key} {value}")
        line = read_until(ser, lambda l: l.startswith("PROV_"), 10, echo=False)
        if not line or not line.startswith(f"PROV_OK {key}"):
            fail(f"Could not write {key} into the board.")
        print(f"  {key} written")
    send(ser, "PROVSHOW")
    line = read_until(ser, lambda l: l.startswith("PROVSHOW"), 10) or ""
    m = re.search(r"cId=(\S*) cEmail=(\S*) cPassLen=(\d+) cKeyLen=(\d+)", line)
    if (not m or m.group(1) != values["cId"] or m.group(2) != values["cEmail"]
            or int(m.group(3)) != len(values["cPass"]) or int(m.group(4)) != len(values["cKey"])):
        fail("Read-back of the cloud identity didn't match.")
    send(ser, "FACTORY_CLEAN")
    if not read_until(ser, lambda l: l == "CLEAN_OK", 10):
        fail("Could not clear the test settings.")
    print("Cloud identity written and verified; test settings cleared.")


# ----------------------------------------------------------------------------- final firmware
def upload_final(port):
    run([pio_exe(), "run", "--project-dir", str(PROJECT_DIR), "-e", FINAL_ENV, "-t", "upload", "--upload-port", port],
        PROJECT_DIR, "Building and uploading the final quanta3 firmware...")


def verify_final(port, device):
    banner("CHECKING THE FINAL FIRMWARE")
    ser = open_port(port)
    found_id = found_fw = found_mac = ""
    deadline = time.time() + 90
    last_poke = 0
    try:
        while time.time() < deadline and not (found_id and found_fw and found_mac):
            if time.time() - last_poke > 10:
                send(ser, "IDENTITY")
                last_poke = time.time()
            line = read_until(ser, lambda l: True, 2)
            if not line:
                continue
            m = re.search(r"DEVICE ID:\s*(\S+)", line)
            if m:
                found_id = m.group(1)
            m = re.search(r"FIRMWARE:\s*(\S+)", line)
            if m:
                found_fw = m.group(1)
            m = re.search(r"MAC Address:\s*([0-9A-Fa-f:]+)", line)
            if m:
                found_mac = m.group(1).upper()
    finally:
        ser.close()
    if found_id != device["deviceId"]:
        fail(f"Final device ID mismatch: '{found_id}' (expected {device['deviceId']})")
    if found_fw != device["firmwareVersion"]:
        fail(f"Final firmware mismatch: '{found_fw}' (expected {device['firmwareVersion']})")
    if found_mac != device["macAddress"].upper():
        fail(f"Final MAC mismatch: '{found_mac}'")
    print(f"\nFinal firmware OK: {found_id}  {found_fw}  {found_mac}")


def write_label(device, claim_code, cfg):
    LABELS_DIR.mkdir(exist_ok=True)
    path = LABELS_DIR / f"{device['deviceId']}.txt"
    text = (f"Quanta Controller\n\nDevice ID: {device['deviceId']}\nSerial Label: {device['label']}\n"
            f"Claim Code: {claim_code}\nMAC Address: {device['macAddress']}\nChip ID: {device['chipId']}\n"
            f"Setup Wi-Fi: {device.get('hotspot', '')}\nLocal page: http://{device['deviceId']}.local\n")
    if cfg.get("setupUrl"):
        text += f"\nSetup:\n{cfg['setupUrl']}\n"
    path.write_text(text, encoding="utf-8")
    return path


def write_report(device, claim_code):
    REPORTS_DIR.mkdir(exist_ok=True)
    path = REPORTS_DIR / f"{device['deviceId']}_{datetime.now():%Y%m%d_%H%M%S}.json"
    safe = {k: v for k, v in device.items() if k != "firebasePassword"}
    path.write_text(json.dumps({"device": safe, "claimCode": claim_code, "completedAt": now_iso(), "log": LOG},
                               indent=4) + "\n", encoding="utf-8")
    return path


# ----------------------------------------------------------------------------- main
def main():
    banner("Quanta Manufacture Device")
    global PRACTICE
    try:
        import quanta_repair                           # puts files right if something is in the wrong place
        quanta_repair.repair_all(PROJECT_DIR)
    except ImportError:
        pass
    version = final_fw_version()
    test_dir(report=True)                              # fail early if Quanta Test is missing or old
    PRACTICE = not ask_yes_no("Is this a real Quanta board?", True)
    if PRACTICE:
        print("\nPRACTICE MODE: ESP32 dev board. Hardware results are shown but don't stop the run;")
        print("no clock, battery or burn-in test; no claim code and nothing written to Firebase.")
        cfg = dict(CLOUD_DEFAULTS)
    else:
        cfg = load_cloud()
    print(f"Final firmware: quanta3 {version}")

    port = choose_port()
    upload_test(port)
    ser = open_port(port)
    wait_test_ready(ser)
    identity = read_identity(ser)
    print(f"\nBoard: MAC {identity['macAddress']}  chip {identity['chipId']}  hotspot {identity['hotspot']}")
    device = enroll(identity, version)

    hardware_test(ser, device)
    if PRACTICE:
        practice_leds(ser, device)
    else:
        zero_outputs(ser, device)
        light_test(ser, device)
    operator_checks(ser, device)
    if PRACTICE:
        claim_code = "PRACTICE"
        device = update_device(device["deviceId"])
    else:
        set_clock(ser)
        ser, port = battery_test(ser, port, device)
        burn_in(ser, device)

        device = update_device(device["deviceId"])
        claim_code, item = reserve_next_claim(device)
        device = update_device(device["deviceId"], claimCode=claim_code, claimBatch=item.get("batch"),
                               claimSequence=item.get("sequence"))
        print(f"\nClaim code assigned: {claim_code}  (batch {item.get('batch')}, #{item.get('sequence')})")
        device = ensure_device_login(cfg, device)
        bind_claim_in_firebase(cfg, device, claim_code, item)

    provision(ser, cfg, device)
    ser.close()
    upload_final(port)
    verify_final(port, device)

    device = update_device(device["deviceId"], manufactureStatus="PRACTICE_COMPLETE" if PRACTICE else "COMPLETE",
                           manufactureCompletedAt=datetime.now().isoformat(timespec="seconds"))
    label = write_label(device, claim_code, cfg)
    report = write_report(device, claim_code)

    banner("PRACTICE RUN COMPLETE (dev board)" if PRACTICE else "MANUFACTURING COMPLETE")
    print(f"Device:           {device['deviceId']}")
    print(f"Label:            {device['label']}")
    print(f"MAC:              {device['macAddress']}")
    print(f"Chip ID:          {device['chipId']}")
    print(f"Setup Wi-Fi:      {device.get('hotspot', '')}")
    print(f"Firmware:         {device['firmwareVersion']}")
    print(f"Claim code:       {claim_code}")
    print(f"Battery test:     {device.get('batteryTest', '?')}")
    if not PRACTICE:
        print("Claim inventory:  ASSIGNED AND VERIFIED")
    print(f"Label file:       {label}")
    print(f"Report:           {report}")
    print("\nPractice OK: this computer can test and program Quanta boards." if PRACTICE else "\nREADY TO PACKAGE")


if __name__ == "__main__":
    main()
