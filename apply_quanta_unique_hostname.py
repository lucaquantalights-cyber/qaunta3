#!/usr/bin/env python3
"""Apply Quanta per-controller mDNS hostnames without replacing the project."""
from pathlib import Path
import re
import shutil
import sys


def find_root(start: Path) -> Path:
    candidates = [start, *start.parents]
    for p in candidates:
        if (p / "src" / "main.cpp").is_file() and (p / "ui" / "index.html").is_file():
            return p
    # Also support running the script from a folder containing an extracted project.
    for main in start.glob("*/src/main.cpp"):
        p = main.parent.parent
        if (p / "ui" / "index.html").is_file():
            return p
    raise FileNotFoundError("Could not find project root (expected src/main.cpp and ui/index.html). Run from your Quanta project folder.")


def backup(path: Path):
    b = path.with_suffix(path.suffix + ".before_unique_hostname")
    if not b.exists():
        shutil.copy2(path, b)


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"Expected exactly one occurrence for {label}, found {count}. No files after this check were changed.")
    return text.replace(old, new, 1)


def apply_main(root: Path):
    p = root / "src/main.cpp"
    t = p.read_text(encoding="utf-8")
    original = t

    t = replace_once(
        t,
        "static String staSsid, staPass;\n",
        "static String staSsid, staPass;\nstatic String mdnsName;                 // unique home-network name derived from the provisioned controller ID\n",
        "mDNS hostname storage",
    )

    helper_anchor = 'static String apName() {\n'
    helper = '''static String controllerMdnsName() {
  // Use the provisioned Quanta device ID so multiple controllers can coexist:
  // quanta3 -> quanta3.local, quanta4 -> quanta4.local, etc.
  String id = prefs.getString("cId", "");
  id.trim(); id.toLowerCase();
  bool valid = id.length() > 0 && id.length() <= 31 && id[0] != '-' && id[id.length() - 1] != '-';
  for (size_t i = 0; valid && i < id.length(); ++i) {
    const char c = id[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) valid = false;
  }
  if (valid) return id;

  // A not-yet-provisioned board still gets a unique hostname instead of sharing quanta.local.
  char fallback[24];
  snprintf(fallback, sizeof(fallback), "%s-%04X", MDNS_FALLBACK_PREFIX,
           (uint16_t)(ESP.getEfuseMac() >> 32));
  return String(fallback);
}

'''
    if "static String controllerMdnsName()" not in t:
        if t.count(helper_anchor) != 1:
            raise RuntimeError("Could not locate apName() to insert the hostname helper.")
        t = t.replace(helper_anchor, helper + helper_anchor, 1)

    # All live mDNS and DHCP hostname operations must use the device-specific name.
    t = t.replace("MDNS.begin(MDNS_NAME)", "MDNS.begin(mdnsName.c_str())")
    t = t.replace("WiFi.setHostname(MDNS_NAME)", "WiFi.setHostname(mdnsName.c_str())")
    t = t.replace("MDNS_NAME);", "mdnsName.c_str());")
    t = t.replace("or http://%s.local\\n", "or http://%s.local\\n")  # keeps printf format, argument is updated below
    t = t.replace("staSsid.c_str(), WiFi.localIP().toString().c_str(), MDNS_NAME);",
                  "staSsid.c_str(), WiFi.localIP().toString().c_str(), mdnsName.c_str());")
    t = t.replace('d["mdns"] = MDNS_NAME;', 'd["mdns"] = mdnsName;')

    # Include the actual hostname in /api/state for the dashboard's Wi-Fi help text.
    if 's["mdns"] = mdnsName;' not in t:
        anchor = '  s["wifi"] = WiFi.status() == WL_CONNECTED ? "connected" : staSsid.length() ? "searching" : "not_set";\n'
        if t.count(anchor) != 1:
            raise RuntimeError("Could not locate /api/state Wi-Fi fields.")
        t = t.replace(anchor, anchor + '  s["mdns"] = mdnsName;\n', 1)

    # Resolve the provisioned ID before setting the ESP32 hostname or starting mDNS.
    if 'mdnsName = controllerMdnsName();' not in t:
        anchor = '  otaAuto = prefs.isKey("otaAuto") ? prefs.getBool("otaAuto", true) : true;\n'
        if t.count(anchor) != 1:
            raise RuntimeError("Could not locate Wi-Fi startup settings.")
        t = t.replace(anchor, anchor + '  mdnsName = controllerMdnsName();\n', 1)

    # If a controller ID is changed through the cloud-setup endpoint, reboot so both
    # the DHCP hostname and mDNS name are re-announced consistently.
    old = '''static void apiCloudSetup() {
  if (server.hasArg("id")) prefs.putString("cId", server.arg("id"));
  if (server.hasArg("email")) prefs.putString("cEmail", server.arg("email"));
'''
    new = '''static void apiCloudSetup() {
  bool hostnameChanged = false;
  if (server.hasArg("id")) {
    prefs.putString("cId", server.arg("id"));
    hostnameChanged = controllerMdnsName() != mdnsName;
  }
  if (server.hasArg("email")) prefs.putString("cEmail", server.arg("email"));
'''
    if new not in t:
        if old not in t:
            raise RuntimeError("Could not locate apiCloudSetup() identity handling.")
        t = t.replace(old, new, 1)
    old_tail = '''  sendJson(r);
}

static void webInit() {
'''
    new_tail = '''  sendJson(r);
  if (hostnameChanged) { delay(250); ESP.restart(); }
}

static void webInit() {
'''
    if new_tail not in t:
        if old_tail not in t:
            raise RuntimeError("Could not locate apiCloudSetup() response end.")
        t = t.replace(old_tail, new_tail, 1)

    # Ensure no fixed live hostname macro references remain in main.cpp.
    if "MDNS_NAME" in t:
        raise RuntimeError("A fixed MDNS_NAME reference remains in main.cpp; review before applying.")

    if t != original:
        backup(p)
        p.write_text(t, encoding="utf-8")


def apply_config(root: Path):
    p = root / "lib/QuantaHal/src/config.h"
    t = p.read_text(encoding="utf-8")
    new = '#define MDNS_FALLBACK_PREFIX "quanta"   // unprovisioned boards use quanta-XXXX.local\n'
    if new not in t:
        old = '#define MDNS_NAME       "quanta"             // http://quanta.local\n'
        if old not in t:
            raise RuntimeError("Could not find the old MDNS_NAME setting in config.h.")
        backup(p)
        p.write_text(t.replace(old, new, 1), encoding="utf-8")


def apply_ui_file(p: Path):
    t = p.read_text(encoding="utf-8")
    original = t
    # Demo state gets a realistic hostname field; live state is supplied by /api/state.
    t = t.replace('ip:"192.168.1.48", ap:false, apName:"Quanta-3F2A",',
                  'ip:"192.168.1.48", mdns:"quanta3", ap:false, apName:"Quanta-3F2A",')
    t = t.replace('On your controller\'s Wi-Fi, open <b>quanta.local</b>.',
                  'On your controller\'s Wi-Fi, open <b>${esc((S.sys.mdns || "quanta") + ".local")}</b>.')
    t = t.replace('Open <b>quanta.local</b> or <b class="num">${s.ip}</b>.',
                  'Open <b>${esc((s.mdns || "quanta") + ".local")}</b> or <b class="num">${s.ip}</b>.')
    t = t.replace('then open quanta.local.`);',
                  'then open ${(S.sys.mdns || "quanta") + ".local"}.`);')
    if "quanta.local" in t:
        raise RuntimeError(f"A fixed quanta.local reference remains in {p}; review before applying.")
    if t != original:
        backup(p)
        p.write_text(t, encoding="utf-8")


def apply_manufacturing(root: Path):
    p = root / "scripts/manufacture_quanta.py"
    t = p.read_text(encoding="utf-8")
    old = 'f"Setup Wi-Fi: {device.get(\'hotspot\', \'\')}\\nLocal page: http://quanta.local\\n")'
    new = 'f"Setup Wi-Fi: {device.get(\'hotspot\', \'\')}\\nLocal page: http://{device[\'deviceId\']}.local\\n")'
    if new not in t:
        if old not in t:
            raise RuntimeError("Could not find manufacturing label's fixed local URL.")
        backup(p)
        p.write_text(t.replace(old, new, 1), encoding="utf-8")


def apply_readme(root: Path):
    p = root / "README.md"
    if not p.exists():
        return
    t = p.read_text(encoding="utf-8")
    original = t
    t = t.replace("`http://quanta.local`", "`http://<device-id>.local`")
    t = re.sub(
        r"Note: every controller answers to `quanta\.local`\. With several controllers on one network, use each one's IP address\.",
        "Each provisioned controller uses its own Device ID as its local hostname. For example, `quanta3` answers at `http://quanta3.local` and `quanta4` answers at `http://quanta4.local`. Unprovisioned boards use a unique `quanta-XXXX.local` fallback name.",
        t,
        count=1,
    )
    if t != original:
        backup(p)
        p.write_text(t, encoding="utf-8")
    elif "every controller answers to `quanta.local`" in t:
        raise RuntimeError("README contains the old multi-controller guidance in an unexpected format.")


def apply_firmware_defaults(root: Path):
    # Point newly built/unprovisioned controllers at Quanta Hosting by default.
    cfg = root / "lib/QuantaHal/src/config.h"
    t = cfg.read_text(encoding="utf-8")
    old_url = '#define OTA_DEFAULT_URL "https://aiesdoser.web.app/quanta/"'
    new_url = '#define OTA_DEFAULT_URL "https://quantalights.web.app/quanta/"'
    if old_url in t:
        backup(cfg)
        cfg.write_text(t.replace(old_url, new_url, 1), encoding="utf-8")

    # The user's earlier request was to remove test mode; keep it disabled in the next build.
    pio = root / "platformio.ini"
    if pio.exists():
        t = pio.read_text(encoding="utf-8")
        updated = re.sub(r"^\s*-D\s+QUANTA_TEST_BUILD=1\s*\r?\n", "", t, flags=re.M)
        if updated != t:
            backup(pio)
            pio.write_text(updated, encoding="utf-8")


def main():
    try:
        root = find_root(Path.cwd().resolve())
        apply_main(root)
        apply_config(root)
        apply_ui_file(root / "ui/index.html")
        # The standalone local dashboard is a separate copy of the UI; keep its help text in sync.
        standalone = root / "quanta-local-dashboard.html"
        if standalone.exists():
            apply_ui_file(standalone)
        apply_manufacturing(root)
        apply_readme(root)
        apply_firmware_defaults(root)
        # Keep the source comments accurate; the displayed URL itself is supplied dynamically.
        main_cpp = root / "src/main.cpp"
        txt = main_cpp.read_text(encoding="utf-8")
        updated = txt.replace("re-announce quanta.local on the home network", "re-announce this controller's unique hostname on the home network")
        updated = updated.replace("Provisioning: http://quanta.local/api/cloud-setup", "Provisioning: http://<device-id>.local/api/cloud-setup")
        updated = updated.replace("instead of sharing quanta.local", "instead of sharing one generic hostname")
        if updated != txt:
            backup(main_cpp)
            main_cpp.write_text(updated, encoding="utf-8")
        # Ensure the HTML served from flash matches the edited UI source even for a plain pio run.
        embed = root / "tools/embed_ui.py"
        if embed.exists():
            import subprocess
            subprocess.run([sys.executable, str(embed)], cwd=root, check=True)
        # Keep comments and pre-generated manufacturing labels accurate as well.
        provision_header = root / "lib/QuantaProvision/src/provision_page.h"
        if provision_header.exists():
            txt = provision_header.read_text(encoding="utf-8")
            updated = txt.replace("http://quanta.local to write down", "the controller-specific .local address to write down")
            if updated != txt:
                backup(provision_header)
                provision_header.write_text(updated, encoding="utf-8")
        provision_meta = root / "lib/QuantaProvision/library.json"
        if provision_meta.exists():
            txt = provision_meta.read_text(encoding="utf-8")
            updated = txt.replace("quanta.local", "controller-specific .local hostname")
            if updated != txt:
                backup(provision_meta)
                provision_meta.write_text(updated, encoding="utf-8")
        labels = root / "labels"
        if labels.is_dir():
            for label in labels.glob("quanta*.txt"):
                txt = label.read_text(encoding="utf-8")
                match = re.search(r"Device ID:\s*(\S+)", txt)
                if match:
                    host = match.group(1).lower()
                    updated = re.sub(r"Local page: http://[^\s]+", f"Local page: http://{host}.local", txt)
                    if updated != txt:
                        backup(label)
                        label.write_text(updated, encoding="utf-8")
    except Exception as exc:
        print(f"PATCH NOT COMPLETED: {exc}", file=sys.stderr)
        sys.exit(1)
    print(f"Unique Quanta hostnames applied successfully in: {root}")
    print("Provisioned IDs now map to their own .local address (for example quanta3.local).")
    print("Backup files ending in .before_unique_hostname were created beside changed files.")
    print("Next: run your normal build/release workflow and publish the new firmware via Quanta Hosting.")

if __name__ == "__main__":
    main()
