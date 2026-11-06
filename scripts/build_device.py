#!/usr/bin/env python3
"""Build one generic Quanta firmware image and publish it to one selected controller's OTA folder.

The selected controller determines only the release destination and manifest deviceId.
No controller ID or per-device credentials are compiled into the binary. The same firmware
source and binary can be copied to additional device folders using --no-bump.
"""

import hashlib
import json
import re
import shutil
import subprocess
import sys
from datetime import datetime
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[1]
DEVICES_FILE = PROJECT_DIR / "scripts" / "devices.json"
MAIN_CPP = PROJECT_DIR / "src" / "main.cpp"
EMBED_UI = PROJECT_DIR / "tools" / "embed_ui.py"
ENV = "esp32dev"
BUILD_BIN = PROJECT_DIR / ".pio" / "build" / ENV / "firmware.bin"
RELEASE_ROOT = PROJECT_DIR / "release" / "quanta" / "device"
CLOUD_FILE = PROJECT_DIR / "scripts" / "quanta_cloud.json"
DEFAULT_FIREBASE_PUBLIC = Path(r"C:\Users\lucaq\OneDrive\Firebase\quanta\firebase\public")
PIO_EXE = Path.home() / ".platformio" / "penv" / "Scripts" / "platformio.exe"
VERSION_RE = re.compile(r'(#define\s+FW_VERSION\s+")([^"]+)(")')
DEVICE_ID_RE = re.compile(r"quanta[0-9]+")


def fail(msg):
    print(f"\nERROR: {msg}")
    raise SystemExit(1)


def ask_yes_no(prompt, default=False):
    suffix = " [Y/n]: " if default else " [y/N]: "
    while True:
        try:
            answer = input(prompt + suffix).strip().lower()
        except EOFError:
            return default
        if not answer:
            return default
        if answer in ("y", "yes"):
            return True
        if answer in ("n", "no"):
            return False
        print("Please answer y or n.")


def load_json(path, description):
    if not path.is_file():
        fail(f"{description} not found:\n{path}")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:
        fail(f"Could not read {description}: {exc}")


def load_devices():
    devices = load_json(DEVICES_FILE, "Manufactured-device registry")
    if not isinstance(devices, dict):
        fail(f"Expected a JSON object in {DEVICES_FILE}.")
    entries = []
    seen = set()
    for key, record in devices.items():
        if not isinstance(record, dict):
            continue
        device_id = str(record.get("deviceId", key)).strip().lower()
        if not DEVICE_ID_RE.fullmatch(device_id):
            continue
        if device_id in seen:
            fail(f"Duplicate device ID in {DEVICES_FILE}: {device_id}")
        seen.add(device_id)
        entries.append((device_id, str(record.get("label", "")).strip(),
                        str(record.get("manufactureStatus", "UNKNOWN")).strip().upper(),
                        bool(record.get("practice", False))))
    entries.sort(key=lambda item: int(item[0][len("quanta"):]))
    if not entries:
        fail(f"No valid quantaN device IDs found in {DEVICES_FILE}.")
    return entries


def select_device(entries):
    print("\nManufactured Quanta controllers:")
    for index, (device_id, label, status, practice) in enumerate(entries, 1):
        details = [status]
        if label:
            details.insert(0, label)
        if practice:
            details.append("PRACTICE - not eligible for customer OTA")
        print(f"  {index}. {device_id}  ({' | '.join(details)})")

    while True:
        answer = input("\nDevice to build/release (ID or list number): ").strip().lower()
        selected = None
        if answer.isdigit() and 1 <= int(answer) <= len(entries):
            selected = entries[int(answer) - 1]
        else:
            selected = next((item for item in entries if item[0] == answer), None)
        if selected is None:
            print("Enter one of the listed device IDs, such as quanta3, or its list number.")
            continue
        device_id, label, status, practice = selected
        if practice or status != "COMPLETE":
            print(f"{device_id} is not a completed production unit (status={status}, practice={practice}). Choose a completed production unit.")
            continue
        print(f"\nSelected device: {device_id}" + (f" ({label})" if label else ""))
        print("The binary remains generic; this selection controls only the per-device release folder and manifest.")
        return device_id


def load_cloud_config():
    if not CLOUD_FILE.exists():
        return {}
    try:
        data = json.loads(CLOUD_FILE.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            return {}
        return data
    except Exception as exc:
        fail(f"Could not read {CLOUD_FILE}: {exc}")


def firebase_public(cfg):
    value = str(cfg.get("firebasePublic", "")).strip().strip('"')
    default = value or str(DEFAULT_FIREBASE_PUBLIC)
    print("\nQuanta Firebase Hosting public folder (the builder adds quanta\\device\\<id>):")
    entered = input(f'Folder [{default}]: ').strip().strip('"')
    path = Path(entered or default).expanduser()
    if path.name.lower() != "public":
        fail(f"Enter the Firebase Hosting public folder itself, ending in \\public. Do not include \\quanta.\nGot: {path}")
    if not path.is_dir():
        fail(f"Firebase public folder does not exist:\n{path}")
    cfg["firebasePublic"] = str(path)
    CLOUD_FILE.parent.mkdir(parents=True, exist_ok=True)
    CLOUD_FILE.write_text(json.dumps(cfg, indent=4) + "\n", encoding="utf-8")
    return path


def pio_exe():
    if PIO_EXE.is_file():
        return str(PIO_EXE)
    for name in ("pio", "platformio"):
        found = shutil.which(name)
        if found:
            return found
    fail(f"PlatformIO not found at {PIO_EXE} or on PATH.")


def read_version():
    if not MAIN_CPP.is_file():
        fail(f"Controller firmware source not found:\n{MAIN_CPP}")
    source = MAIN_CPP.read_text(encoding="utf-8")
    found = VERSION_RE.findall(source)
    if len(found) != 1:
        fail(f"Expected exactly one FW_VERSION definition in {MAIN_CPP}.")
    return source, found[0][1]


def bump_version(version):
    match = re.search(r"(\d+)(?!.*\d)", version)
    if not match:
        fail(f"Cannot increment version without a numeric suffix: {version}")
    return version[:match.start(1)] + str(int(match.group(1)) + 1) + version[match.end(1):]


def copy_verified(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, destination)
    if not destination.is_file() or source.stat().st_size != destination.stat().st_size:
        fail(f"Copy verification failed:\n{source}\n{destination}")
    if hashlib.md5(source.read_bytes()).digest() != hashlib.md5(destination.read_bytes()).digest():
        fail(f"Checksum verification failed after copy:\n{destination}")


def main():
    print("==============================================")
    print("       QUANTA DEVICE OTA BUILDER")
    print("==============================================")
    entries = load_devices()
    device_id = select_device(entries)

    source, current = read_version()
    version = current
    print(f"\nShared firmware source version: {current}")
    if "--no-bump" not in sys.argv:
        next_version = bump_version(current)
        if ask_yes_no(f"Raise FW_VERSION for this release? {current} -> {next_version}", True):
            MAIN_CPP.with_name("main.cpp.bak").write_text(source, encoding="utf-8")
            MAIN_CPP.write_text(VERSION_RE.sub(lambda m: m.group(1) + next_version + m.group(3), source, count=1), encoding="utf-8")
            version = next_version
            print(f"src/main.cpp now says {version}")
    notes = input("Release notes (one line, Enter to skip): ").strip()

    if EMBED_UI.is_file():
        print("\nEmbedding the latest controller dashboard...")
        result = subprocess.run([sys.executable, str(EMBED_UI)], cwd=PROJECT_DIR)
        if result.returncode:
            fail("tools/embed_ui.py failed.")

    if BUILD_BIN.exists():
        BUILD_BIN.unlink()  # prevent accidentally publishing an earlier binary
    print(f"\nBuilding generic Quanta firmware {version}...")
    result = subprocess.run([pio_exe(), "run", "-e", ENV], cwd=PROJECT_DIR)
    if result.returncode or not BUILD_BIN.is_file():
        fail("PlatformIO build failed or firmware.bin was not created.")

    data = BUILD_BIN.read_bytes()
    if not data or data[0] != 0xE9:
        fail("The output does not look like an ESP32 firmware image.")
    if version.encode("ascii") not in data:
        fail(f"The built image does not contain the expected version {version}.")

    manifest = {
        "product": "quanta-altair",
        "deviceId": device_id,
        "version": version,
        "firmware": "firmware.bin",
        "md5": hashlib.md5(data).hexdigest(),
        "size": len(data),
        "notes": notes,
        "builtAt": datetime.now().astimezone().isoformat(timespec="seconds"),
    }

    release_dir = RELEASE_ROOT / device_id
    release_dir.mkdir(parents=True, exist_ok=True)
    release_bin = release_dir / "firmware.bin"
    release_json = release_dir / "firmware.json"
    copy_verified(BUILD_BIN, release_bin)
    release_json.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    public = firebase_public(load_cloud_config())
    firebase_dir = public / "quanta" / "device" / device_id
    firebase_bin = firebase_dir / "firmware.bin"
    firebase_json = firebase_dir / "firmware.json"
    copy_verified(release_bin, firebase_bin)
    shutil.copy2(release_json, firebase_json)
    remote_tool = PROJECT_DIR / "tools" / "build_remote.py"
    if remote_tool.is_file():                     # the website's remote control page, from the same phone page
        if subprocess.run([sys.executable, str(remote_tool), str(public)], cwd=PROJECT_DIR).returncode:
            fail("tools/build_remote.py failed.")
    if json.loads(firebase_json.read_text(encoding="utf-8")) != manifest:
        fail(f"Manifest copy verification failed:\n{firebase_json}")

    print("\n==============================================")
    print("DEVICE RELEASE READY")
    print("==============================================")
    print(f"Device:       {device_id}")
    print(f"Firmware:     {version} (generic image; no device credentials embedded)")
    print(f"Binary size:  {len(data):,} bytes")
    print(f"MD5:          {manifest['md5']}")
    print(f"Local:        {release_dir}")
    print(f"Firebase:     {firebase_dir}")
    print(f"OTA folder:   https://quantalights.web.app/quanta/device/{device_id}/")
    print("\nOnly this device's Firebase folder was updated. No other device release was overwritten.")
    print("Next: firebase deploy --only hosting (run from your Firebase project folder).")
    print("To publish the exact same source version to another device, run with --no-bump and select that device.")


if __name__ == "__main__":
    main()
