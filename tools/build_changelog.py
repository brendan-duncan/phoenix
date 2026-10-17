#!/usr/bin/env python3
"""Writes docs/changelog.html, the guide's What's New page, from CHANGELOG.md.

  python tools/build_changelog.py [--check]

CHANGELOG.md stays the one place releases are written up; this turns it into
a page of the guide, in the same template as the others, so phoenixanimate.com
carries it. tools/deploy_site.py runs it before each deploy. With --check it
writes nothing and fails when docs/changelog.html is out of date.

Only what the changelog uses is understood: "## version" headings, "- "
bullets (a bullet may run on over indented lines), plain paragraphs, and
inline `code`, **bold**, *italic* and [links](url).
"""

import argparse
import html
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "CHANGELOG.md"
TARGET = ROOT / "docs" / "changelog.html"

HEAD = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>What's new · Phoenix User Guide</title>
<meta name="description" content="What changed in each release of Phoenix, newest first.">
<meta property="og:type" content="website">
<meta property="og:site_name" content="Phoenix">
<meta property="og:title" content="What's new · Phoenix User Guide">
<meta property="og:description" content="What changed in each release of Phoenix, newest first.">
<meta property="og:url" content="https://phoenixanimate.com/changelog.html">
<meta property="og:image" content="https://phoenixanimate.com/assets/shots/hero.webp">
<meta property="og:image:type" content="image/webp">
<meta property="og:image:width" content="1704">
<meta property="og:image:height" content="1061">
<meta property="og:image:alt" content="The Phoenix window with three flower characters on the stage, the timeline below and the Properties panel on the right">
<meta name="twitter:card" content="summary_large_image">
<meta name="twitter:image" content="https://phoenixanimate.com/assets/shots/hero.webp">
<meta name="keywords" content="changelog release notes what's new versions updates history">
<link rel="icon" href="assets/img/logo.png">
<link rel="stylesheet" href="assets/site.css">
<script>try { const t = localStorage.getItem("phoenix-docs-theme"); if (t) document.documentElement.dataset.theme = t; } catch (e) {}</script>
<script src="assets/site.js" defer></script>
</head>
<body>
<main>
<article class="content">

<!-- Made from CHANGELOG.md by tools/build_changelog.py: edit that, not this. -->

<p class="eyebrow">Reference</p>
<h1>What's new</h1>
"""

TAIL = """
</article>
</main>
</body>
</html>
"""


def inline(text):
    """Markdown's inline marks as HTML, the rest escaped."""
    out = []
    pattern = re.compile(r"`([^`]+)`|\*\*([^*]+)\*\*|\*([^*]+)\*|\[([^\]]+)\]\(([^)\s]+)\)")
    last = 0
    for m in pattern.finditer(text):
        out.append(html.escape(text[last:m.start()], quote=False))
        if m.group(1) is not None:
            out.append("<code>" + html.escape(m.group(1), quote=False) + "</code>")
        elif m.group(2) is not None:
            out.append("<strong>" + inline(m.group(2)) + "</strong>")
        elif m.group(3) is not None:
            out.append("<em>" + inline(m.group(3)) + "</em>")
        else:
            href = m.group(5)
            # The repository's own Markdown files are on GitHub, not the site.
            if not re.match(r"^[a-z]+:|^#|^/", href):
                href = "https://github.com/brendan-duncan/phoenix/blob/main/" + href
            out.append('<a href="' + html.escape(href) + '">' + inline(m.group(4)) + "</a>")
        last = m.end()
    out.append(html.escape(text[last:], quote=False))
    return "".join(out)


def slug(version):
    return "v" + re.sub(r"[^0-9A-Za-z]+", "-", version).strip("-").lower()


def render(markdown):
    lines = markdown.replace("\r\n", "\n").split("\n")
    body = []
    lead_done = False
    bullets = []  # the bullets of the list being gathered
    paragraph = []

    def flush_list():
        if bullets:
            body.append("<ul>")
            body.extend("  <li>" + inline(item) + "</li>" for item in bullets)
            body.append("</ul>\n")
            bullets.clear()

    def flush_paragraph():
        nonlocal lead_done
        if paragraph:
            text = " ".join(paragraph)
            # The first paragraph is the page's lead line.
            cls = ' class="lead"' if not lead_done else ""
            lead_done = True
            body.append("<p" + cls + ">" + inline(text) + "</p>\n")
            paragraph.clear()

    for line in lines:
        if line.startswith("# "):
            continue  # the page has its own title
        if line.startswith("## "):
            flush_list()
            flush_paragraph()
            version = line[3:].strip()
            body.append('<h2 id="' + slug(version) + '">' + inline(version) + "</h2>\n")
        elif line.startswith("- "):
            flush_paragraph()
            bullets.append(line[2:].strip())
        elif line.startswith("  ") and bullets and line.strip():
            bullets[-1] += " " + line.strip()
        elif not line.strip():
            flush_paragraph()
        else:
            flush_list()
            paragraph.append(line.strip())
    flush_list()
    flush_paragraph()
    return HEAD + "\n" + "\n".join(body).rstrip() + "\n" + TAIL


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true", help="fail if docs/changelog.html is out of date")
    args = parser.parse_args()
    page = render(SOURCE.read_text(encoding="utf-8"))
    current = TARGET.read_text(encoding="utf-8") if TARGET.exists() else None
    if args.check:
        if current != page:
            print("docs/changelog.html is out of date: run python tools/build_changelog.py")
            return 1
        return 0
    if current != page:
        TARGET.write_text(page, encoding="utf-8", newline="\n")
        print("Wrote", TARGET.relative_to(ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
