# Embeds ui/index.html into lib/QuantaWebUI/src/web_ui.h so the controller can serve its phone page.
# PlatformIO runs this automatically before every build (see extra_scripts in platformio.ini).
import os
try:
    Import("env")                                  # running inside PlatformIO
    root = env.subst("$PROJECT_DIR")
except NameError:                                  # running by hand: python3 tools/embed_ui.py
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
src = os.path.join(root, "ui", "index.html")
dst = os.path.join(root, "lib", "QuantaWebUI", "src", "web_ui.h")
body = open(src, encoding="utf-8").read()
html = ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">'
        '<meta name="theme-color" content="#081017"></head><body>' + body + '</body></html>')
import gzip
data = gzip.compress(html.encode("utf-8"), compresslevel=9, mtime=0)   # mtime=0: same page, same bytes
rows = [", ".join(str(b) for b in data[i:i+40]) for i in range(0, len(data), 40)]
out = ("// GENERATED from ui/index.html by tools/embed_ui.py - edit that file instead.\n"
       "// The phone page, gzip-compressed (the controller sends it with Content-Encoding: gzip).\n"
       "#pragma once\n#include <Arduino.h>\n"
       f"const size_t INDEX_HTML_GZ_LEN = {len(data)};\n"
       "const uint8_t INDEX_HTML_GZ[] PROGMEM = {\n" + ",\n".join(rows) + "\n};\n")
if not os.path.exists(dst) or open(dst, encoding="utf-8").read() != out:
    open(dst, "w", encoding="utf-8").write(out)
    print(f"embed_ui: updated lib/QuantaWebUI/src/web_ui.h ({len(html)} bytes -> {len(data)} gzip)")
