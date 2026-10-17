#!/usr/bin/env python3
"""Copies the docs/ site to the web server that hosts phoenixanimate.com.

  python tools/deploy_site.py [--mirror] [--dry-run]

The server, its user, the SSH key and the web root come from
tools/deploy_site.ini, which is kept out of the repository. A first run
writes a template there to fill in:

  [server]
  host = 203.0.113.10
  user = admin
  key = C:\\path\\to\\LightsailDefaultKey-us-west-2.pem
  dir = /var/www/phoenixanimate

The site goes up as one archive over scp and is unpacked over ssh, using the
OpenSSH programs Windows, macOS and Linux all ship. Files on the server are
added and overwritten; --mirror also removes those no longer in docs/. Either
way the folders the server keeps for itself (updates/, download/) are left
alone.

The site analytics client, analytics/phoenix_analytics.js, goes up with it as
assets/analytics.js, stamped with the commit it was deployed from. The
collector it reports to is deployed separately by tools/deploy_analytics.py.
"""

import argparse
import configparser
import io
import shlex
import subprocess
import sys
import tarfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SITE = ROOT / "docs"
CONFIG = Path(__file__).resolve().with_name("deploy_site.ini")

# On the server but not from docs/: the updater's feed and packages, and the
# installers the download links point at.
KEEP = ("updates", "download")

# In docs/ but not for the site.
SKIP_NAMES = {".DS_Store", "Thumbs.db", "desktop.ini"}

# Published next to the docs: the analytics client, which docs/assets/site.js
# loads on phoenixanimate.com only (see analytics/README.md).
ANALYTICS_CLIENT = ROOT / "analytics" / "phoenix_analytics.js"
ANALYTICS_PATH = "assets/analytics.js"

TEMPLATE = """\
; Where tools/deploy_site.py copies docs/. Kept out of the repository.
[server]
host =
user = admin
key =
dir = /var/www/phoenixanimate
"""


def load_config():
    if not CONFIG.exists():
        CONFIG.write_text(TEMPLATE, encoding="utf-8")
        sys.exit(f"Wrote {CONFIG}; fill in host and key, then run again.")
    parser = configparser.ConfigParser()
    parser.read(CONFIG, encoding="utf-8")
    server = parser["server"] if parser.has_section("server") else {}
    config = {name: server.get(name, "").strip() for name in ("host", "user", "key", "dir")}
    missing = [name for name, value in config.items() if not value]
    if missing:
        sys.exit(f"{CONFIG} is missing: {', '.join(missing)}")
    if not Path(config["key"]).is_file():
        sys.exit(f"No key file at {config['key']}")
    # Never anywhere a mistake in it could empty more than the site.
    if not config["dir"].startswith("/var/www/") or config["dir"].rstrip("/") == "/var/www":
        sys.exit(f"dir should be a site's folder under /var/www, not {config['dir']}")
    config["dir"] = config["dir"].rstrip("/")
    return config


def site_files():
    for path in sorted(SITE.rglob("*")):
        if path.is_file() and path.name not in SKIP_NAMES:
            yield path


def build_id():
    """Short commit hash, with "+" when the working tree has changes."""
    try:
        commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
                                capture_output=True, text=True, check=True).stdout.strip()
        dirty = subprocess.run(["git", "status", "--porcelain"], cwd=ROOT,
                               capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""
    return commit + ("+" if dirty else "") if commit.isalnum() else ""


def analytics_client():
    """The analytics client with this deploy's build id in its CONFIG, so
    error reports name the build they came from."""
    js = ANALYTICS_CLIENT.read_text(encoding="utf-8")
    stamped = js.replace("build: '',", f"build: '{build_id()}',", 1)
    if stamped == js:
        sys.exit(f"No build field to stamp in {ANALYTICS_CLIENT}")
    return stamped.encode("utf-8")


def make_archive(files, extra):
    """The site as a .tgz in memory, readable by the web server whoever
    unpacks it. `extra` maps more archive paths to their contents."""
    def normalize(info):
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        info.mode = 0o755 if info.isdir() else 0o644
        return info

    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode="w:gz") as archive:
        for path in files:
            archive.add(path, arcname=path.relative_to(SITE).as_posix(), filter=normalize)
        for name, content in extra.items():
            info = normalize(tarfile.TarInfo(name))
            info.size = len(content)
            info.mtime = int(time.time())
            archive.addfile(info, io.BytesIO(content))
    return data.getvalue()


def ssh_options(config):
    return ["-i", config["key"], "-o", "ConnectTimeout=20"]


def run(cmd):
    result = subprocess.run(cmd)
    if result.returncode != 0:
        sys.exit(f"Failed ({result.returncode}): {' '.join(cmd[:1])} ...")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--mirror", action="store_true",
                        help="also remove files on the server that are no longer in docs/")
    parser.add_argument("--dry-run", action="store_true",
                        help="list what would go up and stop")
    args = parser.parse_args()

    config = load_config()
    files = list(site_files())
    if not files:
        sys.exit(f"Nothing in {SITE}")
    extra = {ANALYTICS_PATH: analytics_client()}
    print(f"{len(files)} files from {SITE} and {ANALYTICS_PATH} -> "
          f"{config['user']}@{config['host']}:{config['dir']}")
    if args.dry_run:
        for path in files:
            print("  " + path.relative_to(SITE).as_posix())
        print(f"  {ANALYTICS_PATH} (from {ANALYTICS_CLIENT.relative_to(ROOT).as_posix()})")
        return

    archive = make_archive(files, extra)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    local = ROOT / "build" / "site.tgz"
    local.parent.mkdir(exist_ok=True)
    local.write_bytes(archive)
    remote_archive = f"/tmp/phoenix-site-{stamp}.tgz"
    remote_stage = f"/tmp/phoenix-site-{stamp}"
    target = f"{config['user']}@{config['host']}"

    print(f"Uploading {len(archive) / 1e6:.1f} MB...")
    run(["scp", *ssh_options(config), str(local), f"{target}:{remote_archive}"])

    # Unpacked to one side first, so a bad archive never touches the site;
    # then cleared (with --mirror) and copied over.
    q = shlex.quote
    site = q(config["dir"])
    steps = [
        "set -e",
        f"mkdir -p {q(remote_stage)} {site}",
        f"tar -xzf {q(remote_archive)} -C {q(remote_stage)}",
    ]
    if args.mirror:
        keep = " ".join(f"! -name {q(name)}" for name in KEEP)
        steps.append(f"find {site} -mindepth 1 -maxdepth 1 {keep} -exec rm -rf {{}} +")
    steps += [
        f"cp -R {q(remote_stage)}/. {site}/",
        f"rm -rf {q(remote_stage)} {q(remote_archive)}",
        f"echo \"$(find {site} -type f | wc -l) files in {config['dir']}\"",
    ]
    print("Unpacking on the server...")
    run(["ssh", *ssh_options(config), target, "; ".join(steps)])
    local.unlink()
    print("Done.")


if __name__ == "__main__":
    main()
