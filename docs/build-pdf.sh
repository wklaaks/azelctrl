#!/usr/bin/env bash
# Build AZELCtrl-Manual.pdf from AZELCtrl-Manual.md
# Pipeline: pandoc (Markdown -> standalone HTML with embedded CSS) -> headless Chrome (print to PDF)
set -euo pipefail
cd "$(dirname "$0")"

MD="AZELCtrl-Manual.md"
CSS="AZELCtrl-Manual.css"
PDF="AZELCtrl-Manual.pdf"
HTML=".AZELCtrl-Manual.build.html"
CHROME="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"

if [ ! -x "$CHROME" ]; then
  echo "ERROR: Chrome not found at $CHROME" >&2
  exit 1
fi

trap 'rm -f "$HTML"' EXIT

pandoc "$MD" \
  --from gfm \
  --standalone \
  --embed-resources \
  --metadata title="AZELCtrl Manual" \
  -c "$CSS" \
  -o "$HTML"

"$CHROME" --headless=new --disable-gpu --no-pdf-header-footer \
  --print-to-pdf="$PDF" "$HTML" >/dev/null 2>&1

echo "Wrote $(basename "$PDF")"
