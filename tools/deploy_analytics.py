#!/usr/bin/env python3
"""Copies the site analytics collector to the server that hosts phoenixanimate.com.

  python tools/deploy_analytics.py [--no-restart] [--dry-run]

Uploads analytics/collector/ (server.mjs, lib/, dashboard/, the systemd unit
and the Apache lines) to /home/admin/phoenix-analytics, then restarts the
phoenix-analytics service if it is installed. The data/ folder there, which
holds the database and the location database, is never touched.

The server, its user and the SSH key come from tools/deploy_site.ini, the same
file tools/deploy_site.py uses. The folder can be changed with an optional
section:

  [analytics]
  dir = /home/admin/phoenix-analytics

Restarting is the default: the dashboard page is read from disk per request,
but the query code only reloads on restart, so an upload without a restart
serves a new page against old APIs. First-time setup (Node, the location
database, the password, the systemd unit and Apache) is in analytics/README.md.
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
COLLECTOR = ROOT / "analytics" / "collector"
CONFIG = Path(__file__).resolve().with_name("deploy_site.ini")
SERVICE = "phoenix-analytics"
DEFAULT_DIR = "/home/admin/phoenix-analytics"

# What goes up from analytics/collector/. Everything else there (data/ from a
# local run, for one) stays on this machine.
FILES = ["server.mjs", "phoenix-analytics.service", "apache.conf", "lib", "dashboard"]
SKIP_NAMES = {".DS_Store", "Thumbs.db", "desktop.ini"}


def load_config():
    if not CONFIG.exists():
        sys.exit(f"No {CONFIG}; run tools/deploy_site.py once to write it, then fill in host and key.")
    parser = configparser.ConfigParser()
    parser.read(CONFIG, encoding="utf-8")
    server = parser["server"] if parser.has_section("server") else {}
    config = {name: server.get(name, "").strip() for name in ("host", "user", "key")}
    missing = [name for name, value in config.items() if not value]
    if missing:
        sys.exit(f"{CONFIG} is missing: {', '.join(missing)}")
    if not Path(config["key"]).is_file():
        sys.exit(f"No key file at {config['key']}")
    section = parser["analytics"] if parser.has_section("analytics") else {}
    folder = (section.get("dir", "") or DEFAULT_DIR).strip().rstrip("/")
    # A home folder's subfolder, never the home folder itself or the web root.
    parts = folder.split("/")
    if not folder.startswith("/home/") or len(parts) < 4 or any(p in ("", ".", "..") for p in parts[1:]):
        sys.exit(f"[analytics] dir should be a folder inside a home folder, not {folder}")
    config["dir"] = folder
    return config


def collector_files():
    for name in FILES:
        path = COLLECTOR / name
        if path.is_dir():
            for child in sorted(path.rglob("*")):
                if child.is_file() and child.name not in SKIP_NAMES:
                    yield child
        elif path.is_file():
            yield path
        else:
            sys.exit(f"Missing {path}")


def make_archive(files):
    def normalize(info):
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        info.mode = 0o755 if info.isdir() else 0o644
        return info

    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode="w:gz") as archive:
        for path in files:
            archive.add(path, arcname=path.relative_to(COLLECTOR).as_posix(), filter=normalize)
    return data.getvalue()


def ssh_options(config):
    return ["-i", config["key"], "-o", "ConnectTimeout=20"]


def run(cmd):
    result = subprocess.run(cmd)
    if result.returncode != 0:
        sys.exit(f"Failed ({result.returncode}): {' '.join(cmd[:1])} ...")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--no-restart", action="store_true",
                        help=f"upload only; don't restart the {SERVICE} service")
    parser.add_argument("--dry-run", action="store_true",
                        help="list what would go up and stop")
    args = parser.parse_args()

    config = load_config()
    files = list(collector_files())
    print(f"{len(files)} files from {COLLECTOR} -> {config['user']}@{config['host']}:{config['dir']}")
    if args.dry_run:
        for path in files:
            print("  " + path.relative_to(COLLECTOR).as_posix())
        return

    archive = make_archive(files)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    local = ROOT / "build" / "analytics-collector.tgz"
    local.parent.mkdir(exist_ok=True)
    local.write_bytes(archive)
    remote_archive = f"/tmp/phoenix-analytics-{stamp}.tgz"
    target = f"{config['user']}@{config['host']}"

    print(f"Uploading {len(archive) / 1e3:.0f} KB...")
    run(["scp", *ssh_options(config), str(local), f"{target}:{remote_archive}"])

    # lib/ and dashboard/ are replaced whole, so files removed here go away
    # there too; data/ is left alone.
    q = shlex.quote
    folder = q(config["dir"])
    steps = [
        "set -e",
        f"mkdir -p {folder}/data",
        f"rm -rf {folder}/lib {folder}/dashboard",
        f"tar -xzf {q(remote_archive)} -C {folder}",
        f"rm -f {q(remote_archive)}",
    ]
    if not args.no_restart:
        steps.append(
            f"if systemctl cat {SERVICE} >/dev/null 2>&1; then "
            f"sudo systemctl restart {SERVICE} && echo \"{SERVICE}: $(systemctl is-active {SERVICE})\"; "
            f"else echo \"{SERVICE} is not installed yet; see analytics/README.md\"; fi")
    print("Unpacking on the server...")
    run(["ssh", *ssh_options(config), target, "; ".join(steps)])
    local.unlink()
    if args.no_restart:
        print(f"Not restarted: run sudo systemctl restart {SERVICE} before using the dashboard.")
    print("Done.")


if __name__ == "__main__":
    main()
