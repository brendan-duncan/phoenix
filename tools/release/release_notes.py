"""Writes the Downloads section of a GitHub release's notes: a row per
platform with its installer and its portable archive, linked to the files
the release workflow uploaded.

    python tools/release/release_notes.py --tag v0.2.0 [--repo owner/name] [--dry-run]

Run by .github/workflows/release.yml once every platform has uploaded. The
section sits between markers, so running it again replaces it and leaves
any other notes alone. Needs the GitHub CLI (gh), signed in.
"""

import argparse
import json
import subprocess
import sys

BEGIN = "<!-- downloads -->"
END = "<!-- /downloads -->"

# Velopack's channels, as tools/release/pack.py names them, in the order
# they are listed, with the name people know each platform by.
PLATFORMS = [
    ("win-x64", "Windows (x64)"),
    ("win-arm64", "Windows (Arm64)"),
    ("osx-arm64", "macOS (Apple silicon)"),
    ("osx-x64", "macOS (Intel)"),
    ("linux-x64", "Linux (x64)"),
    ("linux-arm64", "Linux (Arm64)"),
]


def gh(*args):
    return subprocess.run(["gh", *args], check=True, capture_output=True, text=True).stdout


def downloads(assets, base_url):
    """The Downloads section for a release whose files are named in assets."""
    names = {asset["name"] for asset in assets}
    rows = []
    for channel, label in PLATFORMS:
        installer = next((name for name in sorted(names)
                          if name.startswith(f"Phoenix-{channel}-Setup") or
                          (name.startswith(f"Phoenix-{channel}") and name.endswith(".AppImage"))), None)
        portable = f"Phoenix-{channel}-Portable.zip"
        if not installer and portable not in names:
            continue
        cells = [f"[{installer}]({base_url}/{installer})" if installer else "",
                 f"[{portable}]({base_url}/{portable})" if portable in names else ""]
        rows.append(f"| {label} | {cells[0]} | {cells[1]} |")
    if not rows:
        return ""
    lines = [BEGIN, "## Download", "",
             "| Platform | Installer | Portable |", "| --- | --- | --- |", *rows, "",
             "Installed copies update themselves; the portable archives do not.", END]
    return "\n".join(lines)


def merged(body, section):
    """The notes with the Downloads section put in, or replaced if there."""
    body = body or ""
    if BEGIN in body and END in body:
        before = body[:body.index(BEGIN)]
        after = body[body.index(END) + len(END):]
        return (before + section + after).strip() + "\n"
    return (section + "\n\n" + body).strip() + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--tag", required=True)
    parser.add_argument("--repo", help="owner/name; the current repository if left out")
    parser.add_argument("--dry-run", action="store_true", help="print the notes instead of saving them")
    args = parser.parse_args()

    repo = ["--repo", args.repo] if args.repo else []
    release = json.loads(gh("release", "view", args.tag, *repo, "--json", "assets,body,url"))
    base_url = release["url"].replace("/releases/tag/", "/releases/download/")
    section = downloads(release["assets"], base_url)
    if not section:
        print(f"{args.tag} has no installers to list", file=sys.stderr)
        return 0
    notes = merged(release["body"], section)
    if args.dry_run:
        print(notes)
        return 0
    subprocess.run(["gh", "release", "edit", args.tag, *repo, "--notes-file", "-"],
                   input=notes, text=True, check=True)
    print(f"Updated the downloads of {args.tag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
