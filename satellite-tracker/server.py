#!/usr/bin/env python3
"""Simple HTTP server with SatNOGS CORS proxy for local development."""
import http.server
import json
import urllib.request
import urllib.parse
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8088
SATNOGS_BASE = "https://db.satnogs.org/api"

class Handler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path == "/satnogs-proxy":
            return self.proxy_satnogs(parsed.query)
        return super().do_GET()

    def proxy_satnogs(self, query_string):
        """Proxy request to SatNOGS API and add CORS headers."""
        # Extract the target URL from the query
        # parse_qs splits on & and =, but the value may contain ? which we need to preserve
        # So we manually extract: everything after "url=" is the target
        idx = query_string.find("url=")
        if idx == -1:
            self.send_error(400, "Missing 'url' parameter")
            return
        target = query_string[idx + 4:]  # Everything after "url="
        if not target:
            self.send_error(400, "Missing 'url' parameter")
            return

        # Decode the URL-encoded path
        target = urllib.parse.unquote(target)

        url = SATNOGS_BASE + target
        try:
            req = urllib.request.Request(url)
            with urllib.request.urlopen(req, timeout=10) as resp:
                data = resp.read()
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
                self.send_header("Access-Control-Allow-Headers", "*")
                self.end_headers()
                self.wfile.write(data)
        except urllib.error.HTTPError as e:
            self.send_response(e.code)
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(e.read())
        except Exception as e:
            self.send_error(502, f"Proxy error: {e}")

    def do_OPTIONS(self):
        """Handle CORS preflight."""
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.end_headers()

    def log_message(self, format, *args):
        """Suppress request logging for cleaner output."""
        pass

if __name__ == "__main__":
    print(f"Serving on http://localhost:{PORT}")
    print(f"SatNOGS proxy at http://localhost:{PORT}/satnogs-proxy?url=...")
    server = http.server.HTTPServer(("", PORT), Handler)
    server.serve_forever()
