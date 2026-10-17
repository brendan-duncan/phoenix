#!/usr/bin/env python3
"""Writes the GitHub Pages site: a page for each page of the guide that sends
the reader to the same page on phoenixanimate.com.

  python tools/build_pages_redirect.py OUT_DIR

The guide used to be served from brendan-duncan.github.io/phoenix, and links
to it are still about. .github/workflows/pages.yml publishes what this writes
there instead of docs/, so an old link lands on the right page of
phoenixanimate.com, its #section kept. Every guide page gets one; the format
notes Pages showed from docs/*.md (FLA_FORMAT.html and the like) go to the
pages they became; anything else falls to 404.html, which sends the same path
to the site.
"""

import html
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
SITE = "https://phoenixanimate.com/"
# The notes that were Markdown, as Pages named them, and the pages they became.
FORMER_NOTES = {
    "PHOENIX_FORMAT.html": "phx-format.html",
    "FLA_FORMAT.html": "fla-format.html",
    "BINARY_FLA_FORMAT.html": "binary-fla-format.html",
}

PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Moved to phoenixanimate.com</title>
<link rel="canonical" href="{url}">
<meta http-equiv="refresh" content="0; url={url}">
<script>location.replace({js} + location.search + location.hash);</script>
</head>
<body>
<p>The Phoenix User Guide has moved to <a href="{url}">{url}</a>.</p>
</body>
</html>
"""

# For a path Pages has no page for: the same path on the site, without the
# repository's /phoenix prefix.
NOT_FOUND = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Moved to phoenixanimate.com</title>
<script>
location.replace("https://phoenixanimate.com/" +
  location.pathname.replace(/^\\/phoenix(\\/|$)/, "").replace(/^\\//, "") +
  location.search + location.hash);
</script>
</head>
<body>
<p>The Phoenix User Guide has moved to <a href="https://phoenixanimate.com/">phoenixanimate.com</a>.</p>
</body>
</html>
"""


def page(url):
    js = '"' + url.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return PAGE.format(url=html.escape(url), js=js)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.split("\n\n")[1])
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    count = 0
    for source in sorted(DOCS.glob("*.html")):
        url = SITE if source.name == "index.html" else SITE + source.name
        (out / source.name).write_text(page(url), encoding="utf-8", newline="\n")
        count += 1
    for old, new in FORMER_NOTES.items():
        (out / old).write_text(page(SITE + new), encoding="utf-8", newline="\n")
        count += 1
    (out / "404.html").write_text(NOT_FOUND, encoding="utf-8", newline="\n")
    (out / ".nojekyll").write_text("", encoding="utf-8")
    print(f"Wrote {count} redirects and 404.html to {out}")


if __name__ == "__main__":
    main()
