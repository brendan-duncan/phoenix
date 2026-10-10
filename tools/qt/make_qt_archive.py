#!/usr/bin/env python3
"""Builds a trimmed Qt archive for Phoenix.

Phoenix links Qt Widgets and Svg. A full Qt kit is several gigabytes, most
of it modules Phoenix never touches, so this cuts an installed kit down to
what the build needs and packs it into one archive the CMake configure step
can download (see cmake/PhoenixQt.cmake):

  * the modules Widgets and Svg depend on, found by following the dependency
    lists in Qt's own CMake package files rather than a hand-written list,
    so the same code works for every platform
  * their headers, libraries and CMake package files
  * the plugins a desktop application needs (platforms, image formats,
    styles, icon engines, platform themes and input contexts, and the
    modules those plugins pull in)
  * the build tools (moc, uic, rcc, qmake, windeployqt...)
  * mkspecs, and the Qt licence texts

Qt's package files check that every file an imported target points at
exists, so the trim is driven by those same files: whatever they reference
is kept, and the package files of plugins that are not shipped are removed.

Usage, from a kit already on disk:

  python tools/qt/make_qt_archive.py --target windows-x64 \
      --qtdir C:/Qt/6.11.2/msvc2022_64 --out dist

Or let it fetch the kit with aqtinstall (pip install aqtinstall) first:

  python tools/qt/make_qt_archive.py --target linux-x64 --version 6.11.2 \
      --fetch --out dist

Targets: windows-x64, windows-arm64, macos (universal), linux-x64,
linux-arm64. Build the macOS and Linux archives on that OS: the kits use
symbolic links, which only survive there.

The Windows arm64 kit is a cross kit: Qt's tools for it are x64 programs,
which Windows on ARM runs under emulation. They are taken from an x64 kit
(--host-qtdir, or fetched with --fetch) into a host/ folder in the archive.
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile
from pathlib import Path

ROOT_MODULES = ["Core", "Gui", "Widgets", "Svg"]

# Modules the build tools load at run time without Phoenix linking them:
# windeployqt needs Network. Their libraries are shipped, not their
# headers or package files.
TOOL_RUNTIME_MODULES = ["Network"]

# Plugin folders a desktop Widgets application can need. Anything else under
# plugins/ (sql drivers, multimedia, qml tooling...) is left out.
PLUGIN_DIRS = [
    "platforms", "imageformats", "styles", "iconengines", "generic",
    "platformthemes", "platforminputcontexts", "xcbglintegrations",
    "wayland-decoration-client", "wayland-graphics-integration-client",
    "wayland-shell-integration",
]

TARGETS = {
    # target: (aqt host, aqt arch, archive kind)
    "windows-x64": ("windows", "win64_msvc2022_64", "zip"),
    "windows-arm64": ("windows", "win64_msvc2022_arm64_cross_compiled", "zip"),
    "macos": ("mac", "clang_64", "tar.gz"),
    "linux-x64": ("linux", "linux_gcc_64", "tar.gz"),
    "linux-arm64": ("linux_arm64", "linux_gcc_arm64", "tar.gz"),
}

IMPORT_REF = re.compile(r"\$\{_IMPORT_PREFIX\}/([^\";$]+)")
TARGET_DEPS = re.compile(r"Qt6([A-Za-z0-9_]+)\\;")


def log(message):
    print(message, flush=True)


def read(path):
    return Path(path).read_text(encoding="utf-8", errors="replace")


def import_refs(cmake_file):
    """Paths under the kit that a package file points at."""
    return {ref.rstrip("/") for ref in IMPORT_REF.findall(read(cmake_file))}


def dependencies(cmake_dir, name):
    """Modules a module or plugin package depends on, from its Dependencies
    file: Qt6Core, Qt6Gui... as bare names."""
    deps_file = cmake_dir / f"Qt6{name}Dependencies.cmake"
    if not deps_file.exists():
        return set()
    found = set()
    for line in read(deps_file).splitlines():
        if "_target_deps" in line and "set(" in line:
            found.update(TARGET_DEPS.findall(line))
    return found


class Trim:
    def __init__(self, qtdir, out):
        self.qtdir = Path(qtdir)
        self.out = Path(out)
        self.cmake_root = self.qtdir / "lib" / "cmake"
        if not (self.cmake_root / "Qt6" / "Qt6Config.cmake").exists():
            sys.exit(f"{qtdir} does not look like a Qt 6 kit (no lib/cmake/Qt6/Qt6Config.cmake)")
        self.copied = set()

    # -- copying ---------------------------------------------------------

    def copy(self, relative):
        """Copies one file or directory tree, keeping symlinks as links."""
        relative = Path(relative)
        if str(relative) in self.copied:
            return
        source = self.qtdir / relative
        if not source.exists() and not source.is_symlink():
            return
        destination = self.out / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if source.is_symlink():
            if destination.exists() or destination.is_symlink():
                destination.unlink()
            os.symlink(os.readlink(source), destination)
            # And what it points at, so the link resolves in the archive.
            target = (source.parent / os.readlink(source)).resolve()
            try:
                self.copy(target.relative_to(self.qtdir.resolve()))
            except ValueError:
                pass
        elif source.is_dir():
            shutil.copytree(source, destination, symlinks=True, dirs_exist_ok=True)
        else:
            shutil.copy2(source, destination)
        self.copied.add(str(relative))

    def copy_ref(self, ref):
        """Copies what a package file references, with its companions: a
        shared library's whole version chain, a framework as a whole."""
        # The bare include root is every module's headers; only the module
        # folders under it are wanted, and they are referenced on their own.
        if ref in ("include", "lib", "bin"):
            return
        if ".framework/" in ref:
            self.copy(ref.split(".framework/")[0] + ".framework")
            return
        match = re.match(r"(.*/lib[^/]+?\.so)(\.[0-9.]+)?$", ref)
        if match:
            for sibling in (self.qtdir / match.group(1)).parent.glob(Path(match.group(1)).name + "*"):
                self.copy(sibling.relative_to(self.qtdir))
            return
        self.copy(ref)

    # -- the closure -----------------------------------------------------

    def module_dir(self, module):
        return self.cmake_root / f"Qt6{module}"

    def discover_plugins(self):
        """Every plugin package whose binary sits in a wanted plugins folder:
        {plugin name: (owning module, references)}."""
        plugins = {}
        for targets in self.cmake_root.glob("Qt6*/Qt6*PluginTargets-*.cmake"):
            name = re.match(r"Qt6(.+Plugin)Targets-", targets.name).group(1)
            refs = import_refs(targets)
            wanted = any(ref.split("/")[0] == "plugins" and ref.split("/")[1] in PLUGIN_DIRS
                         for ref in refs)
            if not wanted:
                continue
            owner = targets.parent.name[len("Qt6"):]
            entry = plugins.setdefault(name, (owner, set()))
            entry[1].update(refs)
        return plugins

    def closure(self):
        modules = set(ROOT_MODULES)
        plugins = self.discover_plugins()
        changed = True
        while changed:
            changed = False
            for module in list(modules):
                for dep in dependencies(self.module_dir(module), module):
                    if dep not in modules and self.module_dir(dep).exists():
                        modules.add(dep)
                        changed = True
            for name, (owner, _) in plugins.items():
                # A plugin is shipped when its owner is, and brings its deps.
                if owner not in modules:
                    continue
                for dep in dependencies(self.module_dir(owner), name) | {owner}:
                    if dep not in modules and self.module_dir(dep).exists():
                        modules.add(dep)
                        changed = True
        kept_plugins = {name for name, (owner, _) in plugins.items() if owner in modules}
        return modules, kept_plugins, plugins

    # -- the trim --------------------------------------------------------

    def run(self):
        modules, kept_plugins, plugins = self.closure()
        log(f"modules: {' '.join(sorted(modules))}")
        log(f"plugins: {len(kept_plugins)} of {len(plugins)}")

        # Package files, and everything they reference.
        for module in modules:
            for suffix in ("", "Private", "Tools"):
                cmake_dir = self.cmake_root / f"Qt6{module}{suffix}"
                if not cmake_dir.exists():
                    continue
                self.copy(cmake_dir.relative_to(self.qtdir))
                for cmake_file in cmake_dir.glob("*.cmake"):
                    is_plugin = re.match(r"Qt6(.+Plugin)[A-Z]", cmake_file.name)
                    if is_plugin and is_plugin.group(1) not in kept_plugins:
                        continue
                    for ref in import_refs(cmake_file):
                        self.copy_ref(ref)

        # The module descriptions windeployqt and macdeployqt read to learn
        # which plugin types each module brings.
        for module in modules | set(TOOL_RUNTIME_MODULES):
            self.copy(f"modules/{module}.json")

        # Libraries the tools need at run time, without exposing the module.
        for module in TOOL_RUNTIME_MODULES:
            for targets in self.module_dir(module).glob(f"Qt6{module}Targets-*.cmake"):
                for ref in import_refs(targets):
                    if ref.startswith(("bin/", "lib/")) and "/cmake/" not in ref:
                        self.copy_ref(ref)

        # The package files every Qt6 package needs, the mkspecs qmake and
        # windeployqt read, and the plugin folders as a whole.
        for extra in ("lib/cmake/Qt6", "lib/cmake/Qt6HostInfo", "lib/cmake/Qt6BuildInternals", "mkspecs"):
            self.copy(extra)
        for plugin_dir in PLUGIN_DIRS:
            self.copy(f"plugins/{plugin_dir}")
        # ICU, which Qt's Linux builds carry alongside Core.
        for icu in (self.qtdir / "lib").glob("libicu*"):
            self.copy(icu.relative_to(self.qtdir))

        # Qt's packages list the include root itself as an include folder.
        # On macOS the headers live in the frameworks, so nothing else
        # creates it, and CMake refuses an imported target whose include
        # folder is missing.
        for folder in ("include", "lib", "bin"):
            (self.out / folder).mkdir(parents=True, exist_ok=True)

        self.prune_plugin_packages(kept_plugins)
        self.prune_broken_configs()
        self.copy_licenses()

    def prune_plugin_packages(self, kept_plugins):
        """Package files of plugins that are not shipped would make Qt's
        configure step fail on their missing binaries, so they go."""
        removed = 0
        for cmake_file in (self.out / "lib" / "cmake").glob("Qt6*/Qt6*Plugin*.cmake"):
            name = re.match(r"Qt6(.+Plugin)(?:[A-Z]|$)", cmake_file.name)
            if name and name.group(1) not in kept_plugins:
                cmake_file.unlink()
                removed += 1
        log(f"removed package files of {removed} unshipped plugin entries")

    def prune_broken_configs(self):
        """A per-configuration targets file whose files are not all present
        (a debug configuration on a release-only kit, say) is dropped, since
        Qt checks every one of them."""
        for targets in (self.out / "lib" / "cmake").glob("Qt6*/Qt6*Targets-*.cmake"):
            missing = [ref for ref in import_refs(targets)
                       if not (self.out / ref).exists() and not (self.out / ref).is_symlink()]
            if missing:
                log(f"dropping {targets.relative_to(self.out)}: missing {missing[0]}")
                targets.unlink()

    def copy_licenses(self):
        # The online installer keeps the licences two levels up, beside the
        # version folders; a kit from aqt keeps them one level up.
        # A kit from aqt has none, so the texts kept in the repository
        # (copied from an installer's Licenses folder) stand in.
        for candidate in (self.qtdir.parent.parent / "Licenses", self.qtdir.parent / "Licenses",
                          self.qtdir / "Licenses", Path(__file__).resolve().parent / "licenses"):
            if candidate.exists():
                shutil.copytree(candidate, self.out / "licenses", dirs_exist_ok=True)
                return
        log("warning: no Qt licence texts found; add them to the archive by hand")


def copy_host_tools(host_qtdir, out):
    """For a cross kit: the host kit's build tools and their package files,
    laid out as a Qt prefix QT_HOST_PATH can point at."""
    host = Trim(host_qtdir, out / "host")
    for tools_dir in host.cmake_root.glob("Qt6*Tools"):
        host.copy(tools_dir.relative_to(host.qtdir))
        for cmake_file in tools_dir.glob("*.cmake"):
            for ref in import_refs(cmake_file):
                host.copy(ref)
    for extra in ("lib/cmake/Qt6", "lib/cmake/Qt6HostInfo", "lib/cmake/Qt6BuildInternals", "mkspecs"):
        host.copy(extra)
    # The tools are dynamically linked against Qt's own libraries.
    for module in ("Core", "Gui", "Widgets", "Network", "EntryPointPrivate"):
        cmake_dir = host.module_dir(module)
        if cmake_dir.exists():
            for targets in cmake_dir.glob(f"Qt6{module}Targets-*.cmake"):
                for ref in import_refs(targets):
                    if ref.startswith("bin/") or ref.startswith("lib/"):
                        host.copy_ref(ref)
    host.prune_broken_configs()


# Qt's download server, laid out as the online installer reads it.
QT_REPOSITORY = "https://download.qt.io/online/qtsdkrepository"
HOST_FOLDERS = {"windows": "windows_x86", "linux": "linux_x64", "linux_arm64": "linux_arm64", "mac": "mac_x64"}
WANTED_ARCHIVES = ["qtbase", "qtsvg", "icu", "qtwayland"]


def fetch_kit_direct(version, target, into):
    """Downloads the kit straight from Qt's server, for the repository layout
    Qt 6.11 introduced on Windows (one folder per compiler, which aqtinstall
    3.3 does not know): reads the folder's Updates.xml, takes the base
    package's qtbase and qtsvg archives, and unpacks them."""
    import xml.etree.ElementTree as ElementTree
    import py7zr
    import requests

    host, arch, _ = TARGETS[target]
    major, minor, patch = version.split(".")
    compact = f"{major}{minor}{patch}"
    suffix = arch
    for prefix in ("win64_", "linux_"):
        if suffix.startswith(prefix):
            suffix = suffix[len(prefix):]
    folder = f"{QT_REPOSITORY}/{HOST_FOLDERS[host]}/desktop/qt6_{compact}/qt6_{compact}_{suffix}"
    package = f"qt.qt6.{compact}.{arch}"

    log(f"reading {folder}/Updates.xml")
    response = requests.get(f"{folder}/Updates.xml", timeout=120)
    response.raise_for_status()
    root = ElementTree.fromstring(response.content)
    entry = None
    for update in root.iter("PackageUpdate"):
        if update.findtext("Name", "") == package:
            entry = update
            break
    if entry is None:
        sys.exit(f"{package} is not in {folder}/Updates.xml")

    package_version = entry.findtext("Version", "")
    archives = [name.strip() for name in (entry.findtext("DownloadableArchives", "") or "").split(",")]
    chosen = [name for name in archives if name.split("-")[0] in WANTED_ARCHIVES]
    if not chosen:
        sys.exit(f"no qtbase/qtsvg archives listed for {package}: {archives}")

    # These archives unpack straight into a kit root (bin, lib, include...),
    # so they go under <version>/<kit> as the old layout and aqt lay them.
    kit = Path(into) / version / suffix
    kit.mkdir(parents=True, exist_ok=True)
    for name in chosen:
        url = f"{folder}/{package}/{package_version}{name}"
        download = Path(into) / name
        log(f"downloading {url}")
        with requests.get(url, stream=True, timeout=300) as stream:
            stream.raise_for_status()
            with open(download, "wb") as out:
                for chunk in stream.iter_content(1 << 20):
                    out.write(chunk)
        log(f"unpacking {name}")
        with py7zr.SevenZipFile(download, "r") as archive:
            archive.extractall(path=kit)
        download.unlink()


def fetch_kit(version, target, into):
    """Downloads the kit with aqtinstall, or straight from Qt's server where
    aqtinstall does not know the layout; returns the kit directory."""
    host, arch, _ = TARGETS[target]
    # Qt Svg is one of the base archives in Qt 6, not an add-on module. The
    # Linux kits also split ICU and the Wayland plugins into archives of
    # their own; asking for an archive a kit does not have is refused, so
    # the list is read from aqt first.
    listing = subprocess.run([sys.executable, "-m", "aqt", "list-qt", host, "desktop",
                              "--archives", version, arch], capture_output=True, text=True)
    available = set(listing.stdout.split()) if listing.returncode == 0 else set()
    archives = [name for name in WANTED_ARCHIVES if name in available] if available else ["qtbase", "qtsvg"]
    command = [sys.executable, "-m", "aqt", "install-qt", host, "desktop", version, arch,
               "--outputdir", str(into), "--archives"] + archives
    log(" ".join(command))
    if subprocess.run(command).returncode != 0:
        log("aqtinstall could not fetch the kit; trying Qt's server directly")
        fetch_kit_direct(version, target, into)
    kits = [p for p in (Path(into) / version).iterdir() if p.is_dir()]
    if len(kits) != 1:
        sys.exit(f"expected one kit under {into}/{version}, found {kits}")
    return kits[0]


def write_readme(out, version, target, qtdir):
    (out / "README.md").write_text(
        f"# Qt {version} for Phoenix, {target}\n\n"
        "A subset of the Qt distribution, trimmed to what Phoenix compiles and\n"
        "links against, made by tools/qt/make_qt_archive.py from\n"
        f"`{qtdir}`.\n\n"
        "Qt is used under the LGPL v3: see the licences folder. Phoenix links\n"
        "it dynamically, so it can be replaced with any compatible build of the\n"
        "same version.\n",
        encoding="utf-8")


def pack(out, archive):
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for path in sorted(out.rglob("*")):
                z.write(path, path.relative_to(out))
    else:
        with tarfile.open(archive, "w:gz", compresslevel=6) as tar:
            for path in sorted(out.iterdir()):
                tar.add(path, arcname=path.name, recursive=True)


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def update_cmake_hashes(cmake_file, target, version, digest):
    """Writes the archive's hash into cmake/QtArchives.cmake."""
    key = target.replace("-", "_")
    line = f'set(PHOENIX_QT_SHA256_{key} "{digest}")'
    text = read(cmake_file) if Path(cmake_file).exists() else ""
    pattern = re.compile(rf'^set\(PHOENIX_QT_SHA256_{key} "[^"]*"\)$', re.M)
    if pattern.search(text):
        text = pattern.sub(line, text)
    else:
        text = text.rstrip("\n") + "\n" + line + "\n"
    version_pattern = re.compile(r'^set\(PHOENIX_QT_VERSION "[^"]*"', re.M)
    if version_pattern.search(text):
        text = version_pattern.sub(f'set(PHOENIX_QT_VERSION "{version}"', text)
    Path(cmake_file).write_text(text, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--target", required=True, choices=sorted(TARGETS))
    parser.add_argument("--version", help="Qt version, e.g. 6.11.2 (read from the kit when --qtdir is given)")
    parser.add_argument("--qtdir", help="an installed kit, e.g. C:/Qt/6.11.2/msvc2022_64")
    parser.add_argument("--host-qtdir", help="for windows-arm64: the x64 kit the tools come from")
    parser.add_argument("--fetch", action="store_true", help="download the kit with aqtinstall first")
    parser.add_argument("--out", default="dist", help="where the archive goes")
    parser.add_argument("--keep", action="store_true", help="keep the trimmed folder beside the archive")
    parser.add_argument("--update-cmake", default="cmake/QtArchives.cmake",
                        help="write the hash into this file ('' to skip)")
    args = parser.parse_args()

    work = Path(tempfile.mkdtemp(prefix="phoenix-qt-"))
    try:
        qtdir = Path(args.qtdir) if args.qtdir else None
        host_qtdir = Path(args.host_qtdir) if args.host_qtdir else None
        version = args.version

        if args.fetch:
            if not version:
                sys.exit("--fetch needs --version")
            qtdir = fetch_kit(version, args.target, work / "kit")
            if args.target == "windows-arm64" and not host_qtdir:
                host_qtdir = fetch_kit(version, "windows-x64", work / "host-kit")
        if not qtdir:
            sys.exit("give --qtdir, or --fetch with --version")
        if not version:
            version_file = Path(qtdir) / "lib" / "cmake" / "Qt6" / "Qt6ConfigVersionImpl.cmake"
            match = re.search(r'PACKAGE_VERSION "([^"]+)"', read(version_file))
            version = match.group(1) if match else sys.exit("could not read the kit's version; give --version")
        if args.target == "windows-arm64" and not host_qtdir:
            sys.exit("windows-arm64 needs --host-qtdir (an x64 kit) or --fetch")

        out_dir = Path(args.out)
        out_dir.mkdir(parents=True, exist_ok=True)
        trimmed = out_dir / f"qt-{version}-{args.target}" if args.keep else work / "trimmed"
        if trimmed.exists():
            shutil.rmtree(trimmed)

        log(f"trimming {qtdir} -> {trimmed}")
        Trim(qtdir, trimmed).run()
        if host_qtdir:
            log(f"adding host tools from {host_qtdir}")
            copy_host_tools(host_qtdir, trimmed)
        write_readme(trimmed, version, args.target, qtdir)

        kind = TARGETS[args.target][2]
        archive = out_dir / f"qt-{version}-{args.target}.{kind}"
        log(f"packing {archive}")
        pack(trimmed, archive)
        digest = sha256_of(archive)
        (out_dir / (archive.name + ".sha256")).write_text(f"{digest}  {archive.name}\n")
        size = archive.stat().st_size / 1048576
        log(f"{archive.name}: {size:.1f} MB, sha256 {digest}")

        if args.update_cmake:
            update_cmake_hashes(args.update_cmake, args.target, version, digest)
            log(f"updated {args.update_cmake}")
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
