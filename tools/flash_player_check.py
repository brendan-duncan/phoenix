#!/usr/bin/env python3
"""Plays movies in Adobe's Flash Player and compares what they trace with
what Phoenix's Test Movie traces.

For a document (.fla, .xfl, .phx), Phoenix exports it to SWF
(`Phoenix --export-movie doc out.swf`), plays the document in Test Movie
(`Phoenix play`) and the exported SWF in Flash Player, and compares the two
traces: does the published movie do what Test Movie showed? For an SWF, it
plays the SWF in Flash Player and in Phoenix (imported, then Test Movie):
does Phoenix run the movie as Flash Player does?

    python tools/flash_player_check.py D:/fla/Freya_Fairy_07_2.fla --frames 48
    python tools/flash_player_check.py movies/ --out D:/tmp/flash-check

Flash Player needs the debug projector (flashplayer_32_sa_debug.exe, which
writes trace() to a log), at --flash, FLASH_PLAYER, or
build/flashplayer/flashplayer_32_sa_debug.exe. Adobe still hosts it:
https://fpdownload.macromedia.com/pub/flashplayer/updaters/32/flashplayer_32_sa_debug.exe

The projector runs on a desktop of its own, so no window shows. It reads an
mm.cfg written to the output folder (the environment points it there), but
writes its log where Flash Player always does,
%APPDATA%\\Macromedia\\Flash Player\\Logs\\flashlog.txt, so movies run one at a
time. Flash Player plays in real time: each movie runs for its frames at its
frame rate plus a margin, and Flash Player's trace passes when Phoenix's is
the start of it (a looping movie traces on after Phoenix stops). "Warning:"
lines, which only the debug player writes, are left out.
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import json
import os
import struct
import subprocess
import sys
import time
import zlib

PLAY_REPORT = ("after ", "frame ", "getURL ", "sound ", "fscommand(", "Opening XML file directly")


def header(path):
    """An SWF's frame rate and frame count, or (24, 1)."""
    try:
        with open(path, "rb") as f:
            data = f.read(64 * 1024)
        body = data[8:] if data[:3] == b"FWS" else zlib.decompressobj().decompress(data[8:], 64)
        offset = (5 + 4 * (body[0] >> 3) + 7) // 8
        rate, count = struct.unpack_from("<HH", body, offset)
        return max(1.0, rate / 256.0), max(1, count)
    except (OSError, zlib.error, struct.error, IndexError):
        return 24.0, 1


class Desktop:
    """A desktop of its own, where windows show to no one."""

    def __init__(self, name):
        self.user32 = ctypes.WinDLL("user32", use_last_error=True)
        self.user32.CreateDesktopW.restype = wt.HANDLE
        self.name = name
        self.handle = self.user32.CreateDesktopW(name, None, None, 0, 0x10000000, None)
        if not self.handle:
            raise OSError("could not make a desktop (%d)" % ctypes.get_last_error())

    def close(self):
        self.user32.CloseDesktop(self.handle)


class STARTUPINFOW(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD), ("lpReserved", wt.LPWSTR), ("lpDesktop", wt.LPWSTR), ("lpTitle", wt.LPWSTR),
                ("dwX", wt.DWORD), ("dwY", wt.DWORD), ("dwXSize", wt.DWORD), ("dwYSize", wt.DWORD),
                ("dwXCountChars", wt.DWORD), ("dwYCountChars", wt.DWORD), ("dwFillAttribute", wt.DWORD),
                ("dwFlags", wt.DWORD), ("wShowWindow", wt.WORD), ("cbReserved2", wt.WORD),
                ("lpReserved2", ctypes.c_void_p), ("hStdInput", wt.HANDLE), ("hStdOutput", wt.HANDLE),
                ("hStdError", wt.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [("hProcess", wt.HANDLE), ("hThread", wt.HANDLE), ("dwProcessId", wt.DWORD),
                ("dwThreadId", wt.DWORD)]


def flash_log_path():
    return os.path.join(os.environ["APPDATA"], "Macromedia", "Flash Player", "Logs", "flashlog.txt")


def run_flash(projector, swf, seconds, desktop, home):
    """What the movie traces in Flash Player in \\a seconds."""
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    log = flash_log_path()
    if os.path.exists(log):
        os.remove(log)
    env = dict(os.environ)
    drive, path = os.path.splitdrive(home)
    env.update({"HOMEDRIVE": drive, "HOMEPATH": path})
    block = "".join("%s=%s\0" % item for item in sorted(env.items())) + "\0"
    startup = STARTUPINFOW()
    startup.cb = ctypes.sizeof(startup)
    startup.lpDesktop = desktop.name
    process = PROCESS_INFORMATION()
    command = '"%s" "%s"' % (projector, os.path.abspath(swf))
    CREATE_UNICODE_ENVIRONMENT = 0x400
    if not kernel32.CreateProcessW(None, ctypes.c_wchar_p(command), None, None, False, CREATE_UNICODE_ENVIRONMENT,
                                   ctypes.c_wchar_p(block), os.path.dirname(os.path.abspath(swf)),
                                   ctypes.byref(startup), ctypes.byref(process)):
        raise OSError("could not start Flash Player (%d)" % ctypes.get_last_error())
    try:
        time.sleep(seconds)
    finally:
        kernel32.TerminateProcess(process.hProcess, 0)
        kernel32.WaitForSingleObject(process.hProcess, 5000)
        kernel32.CloseHandle(process.hProcess)
        kernel32.CloseHandle(process.hThread)
    if not os.path.exists(log):
        return []
    with open(log, encoding="utf-8", errors="replace") as f:
        return lines_of(f.read(), lambda line: line.startswith("Warning: "))


def lines_of(text, drop):
    lines = [line.rstrip() for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n")]
    lines = [line for line in lines if not drop(line)]
    while lines and not lines[-1]:
        lines.pop()
    return lines


def run_phoenix_play(phoenix, source, frames, env, timeout):
    done = subprocess.run([phoenix, "play", source, "--frames", str(frames)], capture_output=True, env=env,
                          timeout=timeout)
    text = done.stdout.decode("utf-8", "replace")
    return lines_of(text, lambda line: line.startswith(PLAY_REPORT) or (line.endswith(" errors") and
                                                                       line.split(" ")[0].isdigit()))


def compare(phoenix_lines, flash_lines):
    """'pass' when Phoenix's trace is the start of Flash Player's; else the
    first line that differs."""
    for i, line in enumerate(phoenix_lines):
        theirs = flash_lines[i] if i < len(flash_lines) else "<end>"
        if line != theirs:
            return {"line": i + 1, "phoenix": line, "flash": theirs}
    return None


def check(args, source, desktop, env):
    result = {"source": source}
    name = os.path.splitext(os.path.basename(source))[0]
    folder = os.path.join(args.out, name)
    os.makedirs(folder, exist_ok=True)
    if source.lower().endswith(".swf"):
        swf = source
    else:
        swf = os.path.join(folder, name + ".swf")
        done = subprocess.run([args.phoenix, "--export-movie", source, swf], capture_output=True, env=env,
                              timeout=args.timeout)
        if not os.path.exists(swf):
            result["error"] = "export failed: " + done.stderr.decode("utf-8", "replace").strip()[-300:]
            return result
    rate, count = header(swf)
    frames = args.frames or count
    result["frames"] = frames
    phoenix_lines = run_phoenix_play(args.phoenix, source, frames, env, args.timeout)
    flash_lines = run_flash(args.flash, swf, frames / rate + args.margin, desktop, args.out)
    difference = compare(phoenix_lines, flash_lines)
    result["phoenix_lines"] = len(phoenix_lines)
    result["flash_lines"] = len(flash_lines)
    if difference:
        result["difference"] = difference
    for label, lines in (("phoenix", phoenix_lines), ("flash", flash_lines)):
        with open(os.path.join(folder, label + ".txt"), "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines) + "\n")
    return result


def find_sources(inputs):
    kinds = (".fla", ".xfl", ".phx", ".swf")
    found = []
    for each in inputs:
        if os.path.isdir(each):
            for folder, dirs, files in os.walk(each):
                dirs.sort()
                found += [os.path.join(folder, f) for f in sorted(files) if f.lower().endswith(kinds)]
        elif os.path.isfile(each):
            found.append(each)
    return found


def main():
    if os.name != "nt":
        sys.exit("Flash Player's projector runs on Windows.")
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="+", help="documents or SWF movies, or folders of them")
    parser.add_argument("--flash", default=os.environ.get(
        "FLASH_PLAYER", os.path.join(root, "build", "flashplayer", "flashplayer_32_sa_debug.exe")))
    parser.add_argument("--phoenix", default=os.path.join(root, "build", "Release", "Phoenix.exe"))
    parser.add_argument("--frames", type=int, default=0, help="frames to play (the movie's own count unless given)")
    parser.add_argument("--margin", type=float, default=2.0, help="seconds Flash Player gets to start up")
    parser.add_argument("--out", default=os.path.join(os.environ.get("TEMP", "."), "phoenix-flash-check"))
    parser.add_argument("--timeout", type=float, default=300.0)
    args = parser.parse_args()
    args.phoenix = os.path.abspath(args.phoenix)
    args.out = os.path.abspath(args.out)
    if not os.path.exists(args.flash):
        sys.exit("No Flash Player projector at %s (see --help)" % args.flash)
    os.makedirs(args.out, exist_ok=True)
    # Traces to the log; errors too.
    with open(os.path.join(args.out, "mm.cfg"), "w", newline="\n") as f:
        f.write("ErrorReportingEnable=1\nTraceOutputFileEnable=1\n")
    temp = os.path.join(args.out, "temp")
    os.makedirs(temp, exist_ok=True)
    # Phoenix's settings and SharedObjects apart from yours.
    settings = os.path.join(args.out, "settings")
    os.makedirs(settings, exist_ok=True)
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", TEMP=temp, TMP=temp, PHOENIX_SETTINGS_DIR=settings)

    sources = find_sources(args.inputs)
    desktop = Desktop("PhoenixFlashCheck%d" % os.getpid())
    results = []
    try:
        for source in sources:
            try:
                result = check(args, source, desktop, env)
            except (OSError, subprocess.TimeoutExpired) as error:
                result = {"source": source, "error": str(error)}
            results.append(result)
            if "error" in result:
                verdict = "ERROR " + result["error"]
            elif "difference" in result:
                d = result["difference"]
                verdict = "DIFFERS at line %d: Phoenix %r, Flash Player %r" % (d["line"], d["phoenix"], d["flash"])
            else:
                verdict = "same (%d lines; Flash Player %d)" % (result["phoenix_lines"], result["flash_lines"])
            print("%s: %s" % (source, verdict), flush=True)
    finally:
        desktop.close()
    with open(os.path.join(args.out, "report.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(results, f, indent=1)
    differ = [r for r in results if "error" in r or "difference" in r]
    print("\n%d of %d the same. Report: %s" % (len(results) - len(differ), len(results),
                                             os.path.join(args.out, "report.json")))
    return 1 if differ else 0


if __name__ == "__main__":
    sys.exit(main())
