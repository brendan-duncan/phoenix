#!/usr/bin/env python3
"""Serves a web build of Phoenix on this machine, for trying it in a browser.

  python tools/web/serve.py [build folder] [--port 8000] [--open]

The folder defaults to build-wasm32. Phoenix's threads need the page to be
cross-origin isolated, so every response carries the COOP and COEP headers;
a plain `python -m http.server` does not send them and the page stops with
a SharedArrayBuffer error. It listens on localhost only, which browsers
count as a secure context. Files are served uncached, so a rebuild shows on
the next reload.
"""

import argparse
import http.server
import sys
import webbrowser
from functools import partial
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".js": "text/javascript",
        ".mjs": "text/javascript",
    }

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_GET(self):
        if self.path in ("/", "/index.html") and not (Path(self.directory) / "index.html").exists():
            self.send_response(302)
            self.send_header("Location", "/Phoenix.html")
            self.end_headers()
            return
        super().do_GET()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("folder", nargs="?", default=str(ROOT / "build-wasm32"))
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--open", action="store_true", help="open the page in the default browser")
    args = parser.parse_args()

    folder = Path(args.folder).resolve()
    if not (folder / "Phoenix.wasm").exists():
        sys.exit("%s has no Phoenix.wasm: build it first with tools/web/build_web.py" % folder)

    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.port), partial(Handler, directory=str(folder)))
    url = "http://localhost:%d/" % args.port
    print("Serving %s at %s (Ctrl+C stops)" % (folder, url))
    if args.open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
