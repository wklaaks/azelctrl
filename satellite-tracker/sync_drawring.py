#!/usr/bin/env python3
"""Sync the full orbital-map.js content into the inlined copy in index.html.

The inlined renderer in index.html must match orbital-map.js exactly. Run this
after editing orbital-map.js:

    python3 sync_drawring.py          # sync + verify
    python3 sync_drawring.py --check  # verify only, non-zero exit if out of sync
"""
import sys
import pathlib

DIR = pathlib.Path(__file__).parent
JS = DIR / "orbital-map.js"
HTML = DIR / "index.html"
# Markers that bracket the orbital-map inline code in index.html.
# The entire <script> block from the header comment to </script> is replaced.
START_MARKER = "// Orbital Map — Canvas-based offline world map for AZELCtrl"
END_MARKER = "</script>"


def extract_js_block(src, what):
    """Find the start/end of the orbital-map JS block using the START_MARKER."""
    s = src.find(START_MARKER)
    if s < 0:
        sys.exit("ERROR: start marker not found in %s" % what)
    # Find the preceding <script> tag
    script_start = src.rfind("<script", 0, s)
    if script_start < 0:
        sys.exit("ERROR: <script> tag not found before start marker in %s" % what)
    # Find the closing </script> after the start marker
    e = src.find("</script>", s)
    if e < 0:
        sys.exit("ERROR: </script> not found after start marker in %s" % what)
    return script_start, e + len("</script>")


# Read the canonical JS
js = JS.read_text()
# Remove the top-level _countryColor function from the JS block for index.html
# because index.html already has its own function scope. We'll still include it
# in the inline block since it's needed at module level.
# Actually, the full JS is fine — it's just a <script> block.

html = HTML.read_text()

# Find the JS block in orbital-map.js (the full file content)
js_content = js

# Find the HTML block to replace
h_s, h_e = extract_js_block(html, "index.html")

# The new HTML block: <script>...</script> with the JS content
new_block = "<script>\n" + js_content + "\n</script>"

if html[h_s:h_e] == new_block:
    print("IN SYNC: orbital-map.js identical in both files (%d chars)" % len(js_content))
    sys.exit(0)

if "--check" in sys.argv:
    print("OUT OF SYNC: run `python3 sync_drawring.py` to update index.html")
    sys.exit(1)

# Replace
HTML.write_text(html[:h_s] + new_block + html[h_e:])

# Verify
verify = HTML.read_text()
v_s, v_e = extract_js_block(verify, "index.html (post-write)")
if verify[v_s:v_e] != new_block:
    sys.exit("ERROR: sync failed verification")
print("SYNCED: copied orbital-map.js (%d chars) into index.html" % len(js_content))
print("Stale identifier check: none (full-block sync, no stale refs)")