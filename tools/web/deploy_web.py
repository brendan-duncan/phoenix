#!/usr/bin/env python3
"""Copies Phoenix's web editor to phoenixanimate.com/webeditor.

  python tools/web/deploy_web.py

  more: [--build] [--arch wasm32] [--dry-run] [--configure-apache]

What goes up is the last build, as tools/web/build_web.py packaged it into
<build>/webeditor: the same files `build_web.py --serve` tried. --build
builds it again first.

The server comes from tools/deploy_site.ini, the same one tools/deploy_site.py
uses (its host, user, SSH key and web root); the editor goes into the
webeditor folder under the web root, replacing what was there. The folder
goes up as one archive over scp and is unpacked over ssh to one side, then
swapped in, so the editor is never half updated.

The page only works with the Cross-Origin-Opener-Policy and
Cross-Origin-Embedder-Policy headers. The package's .htaccess sends them when
Apache lets the folder have one (AllowOverride) and mod_headers is on; after
uploading, this checks the live page and says if they are missing.
--configure-apache instead puts them in Apache's own configuration for
/webeditor (a conf-available file, with mod_headers and mod_deflate enabled)
and reloads Apache; it needs sudo on the server, and is only needed once.
"""

import argparse
import configparser
import io
import shlex
import subprocess
import sys
import tarfile
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
CONFIG = ROOT / "tools" / "deploy_site.ini"
FOLDER = "webeditor"
SITE_URL = "https://phoenixanimate.com/" + FOLDER + "/"

APACHE_CONF = """\
# Phoenix's web editor (tools/web/deploy_web.py --configure-apache).
<Location "/webeditor">
    Header always set Cross-Origin-Opener-Policy "same-origin"
    Header always set Cross-Origin-Embedder-Policy "require-corp"
    Header always set Cross-Origin-Resource-Policy "same-origin"
    Header set Cache-Control "no-cache"
    AddOutputFilterByType DEFLATE application/wasm text/javascript application/javascript text/html
</Location>
AddType application/wasm .wasm
"""


def load_config():
    if not CONFIG.exists():
        sys.exit(f"No {CONFIG}: run tools/deploy_site.py once to make it, and fill it in.")
    parser = configparser.ConfigParser()
    parser.read(CONFIG, encoding="utf-8")
    server = parser["server"] if parser.has_section("server") else {}
    config = {name: server.get(name, "").strip() for name in ("host", "user", "key", "dir")}
    missing = [name for name, value in config.items() if not value]
    if missing:
        sys.exit(f"{CONFIG} is missing: {', '.join(missing)}")
    if not Path(config["key"]).is_file():
        sys.exit(f"No key file at {config['key']}")
    if not config["dir"].startswith("/var/www/") or config["dir"].rstrip("/") == "/var/www":
        sys.exit(f"dir should be a site's folder under /var/www, not {config['dir']}")
    config["dir"] = config["dir"].rstrip("/")
    return config


def make_archive(folder):
    def normalize(info):
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        info.mode = 0o755 if info.isdir() else 0o644
        return info

    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode="w:gz") as archive:
        for path in sorted(folder.iterdir()):
            archive.add(path, arcname=path.name, filter=normalize)
    return data.getvalue()


def ssh_options(config):
    return ["-i", config["key"], "-o", "ConnectTimeout=20"]


def run(cmd):
    result = subprocess.run(cmd)
    if result.returncode != 0:
        sys.exit(f"Failed ({result.returncode}): {' '.join(cmd[:1])} ...")


def missing_headers():
    """The isolation headers the live page lacks, or None when it cannot be reached."""
    try:
        request = urllib.request.Request(SITE_URL, method="HEAD")
        with urllib.request.urlopen(request, timeout=20) as response:
            headers = response.headers
    except OSError:
        return None
    wanted = {"Cross-Origin-Opener-Policy": "same-origin", "Cross-Origin-Embedder-Policy": "require-corp"}
    return [name for name, value in wanted.items() if (headers.get(name) or "").strip() != value]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--build", action="store_true", help="build first (tools/web/build_web.py)")
    parser.add_argument("--arch", choices=["wasm32", "wasm64"], default="wasm32")
    parser.add_argument("--dry-run", action="store_true", help="list what would go up and stop")
    parser.add_argument("--configure-apache", action="store_true",
                        help="put the headers in Apache's configuration for /webeditor (needs sudo)")
    args = parser.parse_args()

    if args.build:
        subprocess.run([sys.executable, str(ROOT / "tools" / "web" / "build_web.py"), "--arch", args.arch],
                       check=True)
    package = ROOT / ("build-" + args.arch) / "webeditor"
    if not (package / "Phoenix.wasm").is_file():
        sys.exit(f"No build in {package}: run tools/web/build_web.py first, or pass --build")

    config = load_config()
    target = f"{config['user']}@{config['host']}"
    site = f"{config['dir']}/{FOLDER}"
    files = sorted(path.name for path in package.iterdir())
    print(f"{len(files)} files from {package} -> {target}:{site}")
    if args.dry_run:
        for name in files:
            print("  " + name)
        return

    archive = make_archive(package)
    stamp = time.strftime("%Y%m%d-%H%M%S")
    local = ROOT / "build" / "webeditor.tgz"
    local.parent.mkdir(exist_ok=True)
    local.write_bytes(archive)
    remote_archive = f"/tmp/phoenix-webeditor-{stamp}.tgz"
    remote_stage = f"{config['dir']}/.{FOLDER}-{stamp}"

    print(f"Uploading {len(archive) / 1e6:.1f} MB...")
    run(["scp", *ssh_options(config), str(local), f"{target}:{remote_archive}"])

    # Unpacked beside the editor, then swapped in with two renames.
    q = shlex.quote
    old = f"{config['dir']}/.{FOLDER}-old-{stamp}"
    steps = [
        "set -e",
        f"mkdir -p {q(remote_stage)}",
        f"tar -xzf {q(remote_archive)} -C {q(remote_stage)}",
        f"if [ -d {q(site)} ]; then mv {q(site)} {q(old)}; fi",
        f"mv {q(remote_stage)} {q(site)}",
        f"rm -rf {q(old)} {q(remote_archive)}",
        f"echo \"$(ls {q(site)} | wc -l) files in {site}\"",
    ]
    if args.configure_apache:
        conf = "/etc/apache2/conf-available/phoenix-webeditor.conf"
        steps += [
            f"printf %s {q(APACHE_CONF)} | sudo tee {conf} > /dev/null",
            "sudo a2enmod -q headers deflate",
            "sudo a2enconf -q phoenix-webeditor",
            "sudo apache2ctl configtest",
            "sudo systemctl reload apache2",
        ]
    print("Unpacking on the server...")
    run(["ssh", *ssh_options(config), target, "; ".join(steps)])
    local.unlink()

    missing = missing_headers()
    if missing is None:
        print(f"Done. Could not reach {SITE_URL} to check its headers.")
    elif missing:
        print(f"Done, but {SITE_URL} does not send {', '.join(missing)}: the editor will not start.\n"
              "Run again with --configure-apache to set them in Apache's configuration.")
        return 1
    else:
        print(f"Done: {SITE_URL}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
