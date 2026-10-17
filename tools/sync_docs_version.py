#!/usr/bin/env python3
"""Puts Phoenix's version, as CMakeLists.txt sets it, wherever the guide shows it.

  python tools/sync_docs_version.py [--check]

CMakeLists.txt's project(Phoenix VERSION x.y.z) is the one place a release
sets the version. The guide shows it in docs/assets/site.js (VERSION, which
fills the header, the footer and the pages' version text as they load) and,
for readers without JavaScript and for search engines, in the pages' own
text: elements of class phoenix-version, and the text of elements with a
data-version-text template ("What's new in {}?"). This writes it into all
of them. tools/deploy_site.py runs it before each deploy. With --check it
writes nothing and fails when something is out of date.
"""

import argparse
import html
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
SITE_JS = DOCS / "assets" / "site.js"


def version():
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    found = re.search(r"project\(Phoenix\s+VERSION\s+([0-9][0-9A-Za-z.\-]*)", text)
    if not found:
        sys.exit("CMakeLists.txt has no project(Phoenix VERSION ...)")
    return found.group(1)


def synced_js(text, current):
    return re.sub(r'^(const VERSION = ")[^"]*(";)', lambda m: m.group(1) + current + m.group(2), text, count=1,
                  flags=re.M)


def synced_page(text, current):
    # <span class="phoenix-version">0.16.1</span>
    text = re.sub(r'(<(\w+)[^>]*\bclass="[^"]*\bphoenix-version\b[^"]*"[^>]*>)[^<]*(</\2>)',
                  lambda m: m.group(1) + current + m.group(3), text)

    # <a ... data-version-text="What's new in {}?">What's new in 0.16.1?</a>
    def templated(m):
        template = html.unescape(m.group(3))
        return m.group(1) + html.escape(template.replace("{}", current), quote=False) + m.group(4)

    return re.sub(r'(<(\w+)[^>]*\bdata-version-text="([^"]*)"[^>]*>)[^<]*(</\2>)', templated, text)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true", help="fail if the guide shows another version")
    args = parser.parse_args()
    current = version()
    stale = []
    files = [(SITE_JS, synced_js)] + [(page, synced_page) for page in sorted(DOCS.glob("*.html"))]
    for path, sync in files:
        # Read and written as they are, so line endings stay.
        with open(path, encoding="utf-8", newline="") as f:
            text = f.read()
        updated = sync(text, current)
        if updated == text:
            continue
        stale.append(path.relative_to(ROOT))
        if not args.check:
            with open(path, "w", encoding="utf-8", newline="") as f:
                f.write(updated)
    if args.check:
        if stale:
            print("The guide does not show version %s in: %s; run python tools/sync_docs_version.py"
                  % (current, ", ".join(str(p) for p in stale)))
            return 1
        return 0
    for path in stale:
        print("Version %s in %s" % (current, path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
