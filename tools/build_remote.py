# Makes remote.html for the Quanta website: the controller's own page (ui/index.html) plus the
# Quanta Cloud adapter (ui/remote_adapter.js), so customers can use Now and Schedule from anywhere.
#   python tools/build_remote.py <Firebase public folder>      (build_device.py runs it for you)
import os, sys
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "release")
body = open(os.path.join(root, "ui", "index.html"), encoding="utf-8").read()
adapter = open(os.path.join(root, "ui", "remote_adapter.js"), encoding="utf-8").read()
fb = "".join(f'<script src="/__/firebase/12.1.0/firebase-{m}-compat.js"></script>' for m in ("app", "auth", "database"))
html = ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">'
        '<meta name="theme-color" content="#081017"><title>Quanta Remote</title></head><body>'
        + fb + '<script src="/__/firebase/init.js"></script>'
        + '<script>\n' + adapter + '\n</script>\n' + body + '</body></html>')
os.makedirs(out_dir, exist_ok=True)
dst = os.path.join(out_dir, "remote.html")
open(dst, "w", encoding="utf-8").write(html)
print(f"build_remote: wrote {dst} ({len(html):,} bytes)")
