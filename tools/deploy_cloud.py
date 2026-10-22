#!/usr/bin/env python3
"""Sets up Phoenix Cloud on its server and deploys updates to it.

  python tools/deploy_cloud.py setup                 once: Node, the service, the site, HTTPS
  python tools/deploy_cloud.py deploy                the cloud's code (tested first)
  python tools/deploy_cloud.py deploy --editor       ...and the web editor (build-wasm32/webeditor)
  python tools/deploy_cloud.py deploy --phoenix release|FILE.AppImage
                                                     ...and Phoenix for Linux, for conversions
  python tools/deploy_cloud.py status | logs [-n N] [-f] | restart | rollback
  python tools/deploy_cloud.py backup [--out DIR]    download a copy of the database
  python tools/deploy_cloud.py cli COMMAND ...       node server.mjs COMMAND on the server
                                                     (fonts, check, status, invite, set-password, ...)

The server, its SSH user and key, and the cloud's names come from
tools/deploy_cloud.ini (kept out of the repository; the first run writes one
to fill in). Each deploy goes up as one archive over scp and is put in place
by phoenix-cloud-admin on the server (tools/cloud/), which keeps the release
before and goes back to it if the new one does not start.
"""

import argparse
import configparser
import io
import subprocess
import sys
import tarfile
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLOUD = ROOT / "cloud"
SERVER_FILES = ROOT / "tools" / "cloud"
EDITOR = ROOT / "build-wasm32" / "webeditor"
CONFIG = Path(__file__).resolve().with_name("deploy_cloud.ini")

TEMPLATE = """; Where tools/deploy_cloud.py puts Phoenix Cloud. Kept out of the repository.
[server]
; The Lightsail server's address, its SSH user and the private key for it.
host =
user = admin
key =

[cloud]
; The service's name, and the play origin's (HTML5 versions of documents,
; on a name of its own). Both need DNS A records pointing at the server.
host = cloud.phoenixanimate.com
play_host = cloud-play.phoenixanimate.com
; For Let's Encrypt's notices about the certificate.
email =
"""

# What goes up of the repository: the service, and Phoenix's starter
# templates and tutorials, which New document makes documents from.
APP = [("cloud/server.mjs", CLOUD / "server.mjs"), ("cloud/README.md", CLOUD / "README.md"),
       ("cloud/lib", CLOUD / "lib"), ("cloud/dashboard", CLOUD / "dashboard"),
       ("resources/templates", ROOT / "resources" / "templates"),
       ("resources/tutorials", ROOT / "resources" / "tutorials")]
SKIP_NAMES = {".DS_Store", "Thumbs.db", "desktop.ini"}


def load_config():
    if not CONFIG.exists():
        CONFIG.write_text(TEMPLATE, encoding="utf-8")
        sys.exit(f"Wrote {CONFIG}: fill in the server's host and key, the names and an email, then run this again.")
    parser = configparser.ConfigParser()
    parser.read(CONFIG, encoding="utf-8")
    get = lambda section, name: parser.get(section, name, fallback="").strip()
    config = {
        "host": get("server", "host"), "user": get("server", "user") or "admin", "key": get("server", "key"),
        "cloud_host": get("cloud", "host"), "play_host": get("cloud", "play_host"), "email": get("cloud", "email"),
    }
    missing = [name for name in ("host", "key", "cloud_host", "play_host") if not config[name]]
    if missing:
        sys.exit(f"{CONFIG} is missing: {', '.join(missing)}")
    if not Path(config["key"]).expanduser().is_file():
        sys.exit(f"No key file at {config['key']}")
    config["key"] = str(Path(config["key"]).expanduser())
    return config


def ssh_options(config):
    return ["-i", config["key"], "-o", "ConnectTimeout=20", "-o", "ServerAliveInterval=30"]


def target(config):
    return f"{config['user']}@{config['host']}"


def ssh(config, command, check=True, tty=False):
    cmd = ["ssh", *ssh_options(config), *(["-t"] if tty else []), target(config), command]
    result = subprocess.run(cmd)
    if check and result.returncode != 0:
        sys.exit(f"Failed on the server ({result.returncode}): {command}")
    return result.returncode


def scp_up(config, local, remote):
    result = subprocess.run(["scp", *ssh_options(config), "-q", str(local), f"{target(config)}:{remote}"])
    if result.returncode != 0:
        sys.exit(f"Could not copy {local} to the server")


def scp_down(config, remote, local):
    result = subprocess.run(["scp", *ssh_options(config), "-q", f"{target(config)}:{remote}", str(local)])
    if result.returncode != 0:
        sys.exit(f"Could not copy {remote} from the server")


def git(*args):
    result = subprocess.run(["git", "-C", str(ROOT), *args], capture_output=True, text=True, encoding="utf-8", errors="replace")
    return result.stdout.strip() if result.returncode == 0 else ""


def version():
    """What is being deployed: the commit, marked when cloud/ has changes not committed."""
    commit = git("describe", "--always", "--dirty=-modified", "--exclude", "*") or "unknown"
    return f"{time.strftime('%Y-%m-%d %H:%M', time.gmtime())} UTC {commit}"


def archive(entries, extra):
    """`entries` (archive path, local file or folder) and `extra` (name -> bytes) as a .tgz."""
    def normalize(info):
        info.uid = info.gid = 0
        info.uname = info.gname = ""
        info.mode = 0o755 if info.isdir() else 0o644
        return info

    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode="w:gz") as tar:
        for name, path in entries:
            if not path.exists():
                sys.exit(f"Missing {path}")
            files = [path] if path.is_file() else sorted(p for p in path.rglob("*") if p.is_file())
            for f in files:
                if f.name in SKIP_NAMES:
                    continue
                relative = f.relative_to(path).as_posix()
                arcname = name if f == path else (relative if name == "." else f"{name}/{relative}")
                tar.add(f, arcname=arcname, filter=normalize)
        for name, content in extra.items():
            info = normalize(tarfile.TarInfo(name))
            info.size = len(content)
            info.mtime = int(time.time())
            tar.addfile(info, io.BytesIO(content))
    return data.getvalue()


def unix_text(path):
    """A script or config for the Linux server, with Unix line endings whatever git checked out."""
    return path.read_bytes().replace(b"\r\n", b"\n")


def upload_bytes(config, content, remote_name):
    with tempfile.TemporaryDirectory() as tmp:
        local = Path(tmp) / remote_name
        local.write_bytes(content)
        scp_up(config, local, f"/tmp/{remote_name}")
    return f"/tmp/{remote_name}"


def admin(config, *args, check=True):
    return ssh(config, "sudo phoenix-cloud-admin " + " ".join(args), check=check)


def server_scripts(config):
    """tools/cloud/ to /tmp/phoenix-cloud-setup on the server; that folder."""
    files = {p.name: unix_text(p) for p in sorted(SERVER_FILES.iterdir()) if p.is_file()}
    remote = upload_bytes(config, archive([], files), "phoenix-cloud-setup.tgz")
    folder = "/tmp/phoenix-cloud-setup"
    ssh(config, f"rm -rf {folder} && mkdir -p {folder} && tar -xzf {remote} -C {folder} && rm -f {remote} && chmod +x {folder}/*.sh {folder}/phoenix-cloud-admin")
    return folder


# ── Commands ──────────────────────────────────────────────────────────────

def cmd_setup(config, args):
    if not config["email"]:
        sys.exit(f"{CONFIG} needs an email under [cloud] for Let's Encrypt (or run setup --no-tls).")
    folder = server_scripts(config)
    options = " ".join(o for o, on in (("--no-tls", args.no_tls), ("--swap", args.swap)) if on)
    print(f"Setting up {config['cloud_host']} and {config['play_host']} on {config['host']}...")
    ssh(config, f"sudo bash {folder}/setup_server.sh {config['cloud_host']} {config['play_host']} {config['email']} {options}", tty=True)
    ssh(config, f"rm -rf {folder}")


def run_tests():
    print("Testing the cloud...")
    result = subprocess.run(["node", "--disable-warning=ExperimentalWarning", "--test", "tests/*.test.mjs"],
                            cwd=CLOUD, capture_output=True, text=True, encoding="utf-8", errors="replace")
    summary = [line for line in result.stdout.splitlines() if line.startswith(("# pass", "# fail"))]
    print("  " + ", ".join(s[2:] for s in summary))
    if result.returncode != 0:
        failures = [line for line in result.stdout.splitlines() if line.lstrip().startswith("not ok")]
        sys.exit("Tests failed; nothing was deployed:\n" + "\n".join(failures[:20]))


def latest_appimage(folder):
    """The latest release's Linux AppImage, downloaded with the GitHub CLI."""
    print("Downloading the latest release's AppImage...")
    result = subprocess.run(["gh", "release", "download", "--repo", "brendan-duncan/phoenix", "--pattern", "Phoenix-linux-x64.AppImage",
                             "--dir", str(folder), "--clobber"], cwd=ROOT)
    path = Path(folder) / "Phoenix-linux-x64.AppImage"
    if result.returncode != 0 or not path.exists():
        sys.exit("Could not download it; download Phoenix-linux-x64.AppImage yourself and pass its path to --phoenix.")
    return path


def cmd_deploy(config, args):
    if not args.skip_tests:
        run_tests()
    stamp = version()
    if "-modified" in stamp and git("status", "--porcelain", "--", "cloud", "resources/templates", "resources/tutorials"):
        print("Note: cloud/ has changes that are not committed; they go up as they are.")
    # The scripts on the server first, so the rest is installed by this version of them.
    folder = server_scripts(config)
    ssh(config, f"sudo install -m 755 {folder}/phoenix-cloud-admin /opt/phoenix-cloud/bin/phoenix-cloud-admin && "
                f"sudo install -m 644 {folder}/phoenix-cloud.service /etc/systemd/system/phoenix-cloud.service && "
                f"sudo systemctl daemon-reload && rm -rf {folder}")

    # The service first: the editor and Phoenix go in beside a running one.
    content = archive(APP, {"VERSION": stamp.encode()})
    print(f"Uploading the cloud ({len(content) / 1024:.0f} KB, {stamp})...")
    remote = upload_bytes(config, content, "phoenix-cloud-app.tgz")
    admin(config, "install-app", remote)

    if args.editor:
        if not (EDITOR / "index.html").exists():
            sys.exit(f"No web editor build in {EDITOR}; build it with python tools/web/build_web.py")
        content = archive([(".", EDITOR)], {"VERSION": stamp.encode()})
        print(f"Uploading the web editor ({len(content) / 1048576:.0f} MB)...")
        remote = upload_bytes(config, content, "phoenix-cloud-editor.tgz")
        admin(config, "install-editor", remote)

    if args.phoenix:
        with tempfile.TemporaryDirectory() as tmp:
            image = latest_appimage(tmp) if args.phoenix == "release" else Path(args.phoenix)
            if not image.is_file():
                sys.exit(f"No file {image}")
            print(f"Uploading Phoenix ({image.stat().st_size / 1048576:.0f} MB)...")
            scp_up(config, image, "/tmp/Phoenix.AppImage")
        admin(config, "install-phoenix", "/tmp/Phoenix.AppImage")
    print(f"Deployed: https://{config['cloud_host']}/")


def cmd_backup(config, args):
    out = Path(args.out or ROOT / "cloud-backups")
    out.mkdir(parents=True, exist_ok=True)
    result = subprocess.run(["ssh", *ssh_options(config), target(config), "sudo phoenix-cloud-admin backup"], capture_output=True, text=True, encoding="utf-8", errors="replace")
    remote = result.stdout.strip().splitlines()[-1] if result.stdout.strip() else ""
    if result.returncode != 0 or not remote.startswith("/tmp/"):
        sys.exit(f"The backup failed: {result.stderr.strip() or result.stdout.strip()}")
    local = out / Path(remote).name
    scp_down(config, remote, local)
    ssh(config, f"rm -f {remote}")
    print(f"{local} ({local.stat().st_size / 1024:.0f} KB). Documents' contents are on the server only: keep Lightsail snapshots too.")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0], formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    setup = commands.add_parser("setup", help="first-time setup of the server")
    setup.add_argument("--no-tls", action="store_true", help="no Let's Encrypt certificate (names not pointing here yet)")
    setup.add_argument("--swap", action="store_true", help="add 2 GB of swap when the server has none")
    deploy = commands.add_parser("deploy", help="send the cloud (and optionally the editor and Phoenix) up")
    deploy.add_argument("--editor", action="store_true", help="also the web editor from build-wasm32/webeditor")
    deploy.add_argument("--phoenix", metavar="release|FILE", help="also Phoenix for Linux: the latest release's AppImage, or one you have")
    deploy.add_argument("--skip-tests", action="store_true", help="deploy without running the cloud's tests first")
    commands.add_parser("status", help="what runs, and the totals")
    logs = commands.add_parser("logs", help="the service's log")
    logs.add_argument("-n", type=int, default=100, help="how many lines (default 100)")
    logs.add_argument("-f", "--follow", action="store_true", help="keep showing new lines")
    commands.add_parser("restart", help="restart the service (after changing /etc/phoenix-cloud.env)")
    commands.add_parser("rollback", help="go back to the release before")
    backup = commands.add_parser("backup", help="download a copy of the database")
    backup.add_argument("--out", help="folder to put it in (default: cloud-backups/ here, which git ignores)")
    cli = commands.add_parser("cli", help="run node server.mjs COMMAND on the server")
    cli.add_argument("args", nargs=argparse.REMAINDER)
    args = parser.parse_args()

    config = load_config()
    if args.command == "setup":
        cmd_setup(config, args)
    elif args.command == "deploy":
        cmd_deploy(config, args)
    elif args.command == "status":
        admin(config, "status")
    elif args.command == "logs":
        ssh(config, "sudo phoenix-cloud-admin follow" if args.follow else f"sudo phoenix-cloud-admin logs {args.n}", tty=args.follow)
    elif args.command in ("restart", "rollback"):
        admin(config, args.command)
    elif args.command == "backup":
        cmd_backup(config, args)
    elif args.command == "cli":
        if not args.args:
            sys.exit("Which command? For example: cli fonts, cli check, cli invite --email someone@example.com")
        quoted = " ".join("'" + a.replace("'", "'\\''") + "'" for a in args.args)
        sys.exit(ssh(config, f"sudo phoenix-cloud-admin cli {quoted}", check=False))


if __name__ == "__main__":
    main()
