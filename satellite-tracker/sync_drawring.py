#!/usr/bin/env python3
"""Sync the _drawRing() method from orbital-map.js into the inlined copy in index.html.

The inlined renderer in index.html must match orbital-map.js exactly. Run this
after editing _drawRing() in orbital-map.js:

    python3 sync_drawring.py          # sync + verify
    python3 sync_drawring.py --check  # verify only, non-zero exit if out of sync
"""
import sys
import pathlib

DIR = pathlib.Path(__file__).parent
JS = DIR / "orbital-map.js"
HTML = DIR / "index.html"
MARKER = "  _drawRing(ring) {"


def extract(src, what):
    start = src.find(MARKER)
    if start < 0:
        sys.exit("ERROR: %s not found in %s" % (MARKER.strip(), what))
    if src.find(MARKER, start + 1) >= 0:
        sys.exit("ERROR: multiple _drawRing definitions in %s" % what)
    depth = 0
    i = src.index("{", start)
    while i < len(src):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return start, i + 1
        i += 1
    sys.exit("ERROR: unbalanced braces in %s" % what)


js = JS.read_text()
html = HTML.read_text()
js_s, js_e = extract(js, "orbital-map.js")
method = js[js_s:js_e]
h_s, h_e = extract(html, "index.html")

if method == html[h_s:h_e]:
    print("IN SYNC: _drawRing() identical in both files (%d chars)" % len(method))
    sys.exit(0)

if "--check" in sys.argv:
    sys.exit("OUT OF SYNC: run `python3 sync_drawring.py` to update index.html")

HTML.write_text(html[:h_s] + method + html[h_e:])
verify = HTML.read_text()
v_s, v_e = extract(verify, "index.html")
if verify[v_s:v_e] != method:
    sys.exit("ERROR: sync failed verification")
print("SYNCED: copied _drawRing() (%d chars) from orbital-map.js into index.html" % len(method))

leftovers = {k: verify.count(k) for k in ("closeMode", "wrapsGlobe", "isFullWidth", "touchesLeft")}
stale = {k: v for k, v in leftovers.items() if v}
print("stale heuristic identifiers remaining in index.html: %s" % (stale or "none"))
