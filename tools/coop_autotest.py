"""Automatic in-game tests for New Vegas Co-op, with two copies of the game on this PC.

Each copy opens the mod's lab channel (LabChannel=1 in NVCoop.ini): a named pipe that takes one line and answers
one line, run from the game's main loop (src/main.cpp RunLabLine: status, menu host, pick N, approve yes, console
lines, eval, ...). The first copy is \\\\.\\pipe\\NVCoopLab, the second NVCoopLab-2. This script starts both copies,
carries out a test script against them, takes screenshots of each window, and writes a report.

    python -I tools/coop_autotest.py deploy --yes     # the build into the game (Vortex copies moved aside), LabChannel=1
    python -I tools/coop_autotest.py check            # is the game's NVCoop.dll this build?
    python -I tools/coop_autotest.py saves            # make the two test saves from the newest save (once)
    python -I tools/coop_autotest.py run connect [more tests] [--quit] [--timeout 900]
    python -I tools/coop_autotest.py lab host "status"  # one line to a running copy (exploring)
    python -I tools/coop_autotest.py undeploy         # put the game back as it was

Test scripts (tests/autotest/*.txt), one step a line; <who> is host, friend or both:
    <who>: <lab line>                    send it; the answer goes in the report
    expect <who> <lab line> ~ <text>     the answer must contain <text>  (!~ : must not)
    waitfor <who> <secs> <lab line> ~ <text>   ask every second until the answer contains <text>
    wait <secs>                          sleep
    shot <who> <name>                    screenshot of that copy's window
    say <text>                           a note in the report
    include <test>                       another test's lines
    launch                               start both copies (host first) and wait for their lab channels
    quit                                 both copies quit (quitgame), killed after 30 s if still up

Saves: the tests only load "Autotest Host" and "Autotest Friend" (made by `saves`). Saves a run writes are listed
before and after; the new ones are deleted at the end, never anything that was there before.
"""
from __future__ import annotations

import argparse
import ctypes
import datetime as dt
import glob
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from ctypes import wintypes

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TESTS = os.path.join(ROOT, "tests", "autotest")
OUT = os.path.join(ROOT, "build", "autotest")
BUILT_DLL = os.path.join(ROOT, "build", "Release", "NVCoop.dll")

GAME = os.environ.get("FNV_DIR", r"D:\SteamLibrary\steamapps\common\Fallout New Vegas")
GAME_EXE = "FalloutNV.exe"
PLUGINS = os.path.join(GAME, "Data", "NVSE", "Plugins")
GAME_DLL = os.path.join(PLUGINS, "NVCoop.dll")
GAME_INI = os.path.join(PLUGINS, "NVCoop.ini")
PIPES = {"host": r"\\.\pipe\NVCoopLab", "friend": r"\\.\pipe\NVCoopLab-2"}
LOGS = {"host": "NVCoop.log", "friend": "NVCoop-2.log"}
SAVE_NAMES = {"host": "Autotest Host", "friend": "Autotest Friend"}


def documents() -> str:
    buf = ctypes.create_unicode_buffer(wintypes.MAX_PATH)
    if ctypes.windll.shell32.SHGetFolderPathW(None, 5, None, 0, buf) == 0:
        return buf.value
    return os.path.join(os.path.expanduser("~"), "Documents")


MYGAMES = os.path.join(documents(), "My Games", "FalloutNV")


def save_folder() -> str:
    # Vortex profiles keep their saves in a subfolder (SLocalSavePath in Fallout.ini)
    try:
        with open(os.path.join(MYGAMES, "Fallout.ini"), encoding="utf-8", errors="replace") as f:
            for line in f:
                if line.strip().lower().startswith("slocalsavepath="):
                    return os.path.join(MYGAMES, line.split("=", 1)[1].strip().rstrip("\\/"))
    except OSError:
        pass
    return os.path.join(MYGAMES, "Saves")


# ---- processes and windows

def game_pids() -> list[int]:
    out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {GAME_EXE}", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    pids = []
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) > 1 and parts[0].strip('"').lower() == GAME_EXE.lower():
            try:
                pids.append(int(parts[1]))
            except ValueError:
                pass
    return pids


def window_of(pid: int) -> int:
    """The biggest visible top-level window of a process (the game's), 0 if none."""
    user32 = ctypes.windll.user32
    found: list[tuple[int, int]] = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def each(hwnd, _):
        owner = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            r = wintypes.RECT()
            user32.GetWindowRect(hwnd, ctypes.byref(r))
            found.append(((r.right - r.left) * (r.bottom - r.top), hwnd))
        return True

    user32.EnumWindows(each, 0)
    return max(found)[1] if found else 0


def focus(hwnd: int) -> bool:
    """Brings a window to the front (Windows only lets the foreground program do that, so tap Alt first)."""
    user32 = ctypes.windll.user32
    if not hwnd:
        return False
    user32.keybd_event(0x12, 0, 0, 0)
    user32.keybd_event(0x12, 0, 2, 0)
    user32.ShowWindow(hwnd, 9)  # SW_RESTORE
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.3)
    return user32.GetForegroundWindow() == hwnd


def copies_by_age() -> list[int]:
    """Running game copies, oldest first (the host is started first)."""
    k32 = ctypes.windll.kernel32
    ages = []
    for pid in game_pids():
        h = k32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        c, e, k, u = (wintypes.FILETIME() for _ in range(4))
        if h and k32.GetProcessTimes(h, ctypes.byref(c), ctypes.byref(e), ctypes.byref(k), ctypes.byref(u)):
            ages.append(((c.dwHighDateTime << 32) | c.dwLowDateTime, pid))
        if h:
            k32.CloseHandle(h)
    return [pid for _, pid in sorted(ages)]


def ffmpeg() -> str:
    for c in (os.environ.get("FFMPEG"), r"C:\ffmpeg\bin\ffmpeg.exe", shutil.which("ffmpeg")):
        if c and os.path.isfile(c):
            return c
    raise SystemExit("ffmpeg not found (set FFMPEG); it needs gfxcapture for screenshots")


def grab(out: str, hwnd: int, wait: float = 10.0) -> bool:
    """One frame of a window (gfxcapture by handle: the real GPU picture, even when covered). ffmpeg writes the
    picture and then doesn't exit on this PC: wait for the file to stop growing, then end it."""
    try:
        os.remove(out)
    except FileNotFoundError:
        pass
    p = subprocess.Popen([ffmpeg(), "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                          f"gfxcapture=hwnd={hwnd}:capture_cursor=0,hwdownload,format=bgra", "-frames:v", "1", out],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    end, size, ok = time.time() + wait, -1, False
    while time.time() < end:
        if p.poll() is not None:
            ok = p.returncode == 0 and os.path.isfile(out)
            break
        if os.path.isfile(out):
            s = os.path.getsize(out)
            if s > 0 and s == size:
                ok = True
                break
            size = s
        time.sleep(0.25)
    if p.poll() is None:
        p.kill()
        p.wait(5)
    return ok and os.path.isfile(out) and os.path.getsize(out) > 0


def sha(path: str) -> str | None:
    if not os.path.isfile(path):
        return None
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


# ---- the lab channel

class Lab:
    def __init__(self, who: str):
        self.who = who
        self.pipe = None

    def open(self, wait: float = 0) -> bool:
        end = time.time() + wait
        while True:
            try:
                self.pipe = open(PIPES[self.who], "r+b", buffering=0)
                return True
            except OSError:
                if time.time() >= end:
                    return False
                time.sleep(1)

    def ask(self, line: str) -> str:
        if not self.pipe and not self.open():
            return "no lab channel (game not running, or LabChannel=0)"
        try:
            self.pipe.write((line + "\n").encode("utf-8"))
            got = b""
            while not got.endswith(b"\n"):
                ch = self.pipe.read(1)
                if not ch:
                    raise OSError("closed")
                got += ch
            return got.decode("utf-8", errors="replace").rstrip("\r\n")
        except OSError as e:
            self.close()
            return f"lab channel lost ({e})"

    def close(self) -> None:
        if self.pipe:
            try:
                self.pipe.close()
            except OSError:
                pass
        self.pipe = None


# ---- tests

def test_path(name: str) -> str:
    for p in (name, os.path.join(TESTS, name), os.path.join(TESTS, name + ".txt")):
        if os.path.isfile(p):
            return p
    raise SystemExit(f"no test called {name} (see: coop_autotest.py list)")


def expand(path: str, seen: set[str] | None = None) -> list[str]:
    seen = seen or set()
    real = os.path.abspath(path)
    if real in seen:
        raise SystemExit(f"{path} includes itself")
    seen = seen | {real}
    lines = []
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.rstrip("\r\n")
            w = line.split()
            if w[:1] == ["include"] and len(w) == 2:
                lines.append(f"say --- {w[1]}")
                lines += expand(test_path(w[1]), seen)
            elif w[:1] == ["repeat"] and len(w) == 3 and w[1].isdigit():
                # repeat <n> <test>: that test n times in a row (long tests)
                for i in range(int(w[1])):
                    lines.append(f"say --- {w[2]} ({i + 1} of {w[1]})")
                    lines += expand(test_path(w[2]), seen)
            else:
                lines.append(line)
    return lines


def describe(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        first = f.readline().strip()
    return first.lstrip("# ") if first.startswith("#") else ""


def first_number(text: str) -> float | None:
    import re
    m = re.search(r"-?\d+(?:\.\d+)?", text)
    return float(m.group()) if m else None


def check(got: str, op: str, want: str) -> bool:
    if op == "~":
        return want.lower() in got.lower()
    if op == "!~":
        return want.lower() not in got.lower()
    if op == "==":
        return got.strip() == want
    v, x = first_number(got), first_number(want)
    if v is None or x is None:
        return False
    return v >= x if op == ">=" else v <= x


def norm(text: str) -> str:
    """For comparing the two copies: whole numbers, list parts in any order, and a container's own reference id
    left out (each copy may have its own copy of an NPC)."""
    import re
    text = re.sub(r"-?\d+\.\d+", lambda m: str(round(float(m.group()))), text.strip())
    text = re.sub(r"\(reading took [^)]*\)", "", text)
    text = re.sub(r"shows [0-9A-F]{8} \(base [0-9A-F]{8}\)", "shows", text)
    return " | ".join(sorted(p.strip() for p in text.split(" | ")))


def whos(w: str) -> list[str]:
    return ["host", "friend"] if w == "both" else [w]


class Run:
    def __init__(self, names: list[str], args):
        self.args = args
        stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
        self.dir = os.path.join(OUT, stamp + "-" + "+".join(os.path.splitext(os.path.basename(n))[0]
                                                               for n in names)[:60])
        os.makedirs(self.dir, exist_ok=True)
        self.lines: list[str] = []
        for n in names:
            self.lines.append(f"say test {n}")
            self.lines += expand(test_path(n))
        if args.quit and not any(l.strip() == "quit" for l in self.lines):
            self.lines.append("quit")
        self.labs = {w: Lab(w) for w in ("host", "friend")}
        self.pids: dict[str, int] = {}
        self.launched: list[int] = []
        self.log_lines: list[str] = []
        self.passed: list[str] = []
        self.failed: list[str] = []
        self.shots: list[str] = []
        self.started = time.time()
        self.outcome = "unknown"
        self.vars: dict[str, str] = {}
        self.saves_before = set(os.listdir(save_folder())) if os.path.isdir(save_folder()) else set()

    def log(self, msg: str) -> None:
        line = f"[{time.time() - self.started:7.1f}] {msg}"
        print(line, flush=True)
        self.log_lines.append(line)

    def ok(self, n: int, what: str, seen: str) -> None:
        self.passed.append(f"line {n}: {what}")
        self.log(f"PASS line {n}: {what}  ({seen})")

    def bad(self, n: int, what: str, seen: str) -> None:
        self.failed.append(f"line {n}: {what}  ({seen})")
        self.log(f"FAIL line {n}: {what}  ({seen})")

    def crashed(self) -> list[str]:
        alive = set(game_pids())
        return [w for w, pid in self.pids.items() if pid not in alive]

    # launch both copies: the host first (it takes NVCoop.log and the first lab channel), then the friend
    def launch(self) -> bool:
        if game_pids():
            self.log("the game is already running: close it first (the runner only drives copies it started)")
            return False
        for who in ("host", "friend"):
            if not self.launch_one(who):
                return False
        self.mark()
        return True

    def launch_one(self, who: str) -> bool:
        if True:
            before = set(game_pids())
            subprocess.Popen([os.path.join(GAME, GAME_EXE)], cwd=GAME)
            for _ in range(90):
                time.sleep(1)
                new = [p for p in game_pids() if p not in before]
                if new:
                    self.pids[who] = new[0]
                    self.launched.append(new[0])
                    break
            if who not in self.pids:
                self.log(f"{who}: the game didn't start in 90 s")
                return False
            self.log(f"{who}: pid {self.pids[who]}; waiting for its lab channel")
            if not self.labs[who].open(wait=180):
                self.log(f"{who}: no lab channel in 180 s (is LabChannel=1? did it crash?)")
                return False
            # while it still has the focus: keep running in the background (else the copy behind stops at its
            # opening screen), and no autosaves (they'd write over the player's own autosave slots). In memory only.
            for setting in ("bAlwaysActive:General", "bSaveOnTravel:GamePlay", "bSaveOnWait:GamePlay",
                            "bSaveOnRest:GamePlay", "bSaveOnInteriorExteriorSwitch:GamePlay"):
                value = 1 if setting.startswith("bAlways") else 0
                self.labs[who].ask(f'SetNumericINISetting "{setting}" {value}')
            self.log(f"{who}: {self.labs[who].ask('status')} (background running on, autosaves off)")
        return True

    def ask(self, who: str, line: str) -> str:
        """A lab line to one copy ($names replaced). For the mod's dev commands (coop ...), which print to the
        console and the log, the answer is what they wrote to the log ("Command: ..." lines)."""
        for k, v in sorted(self.vars.items(), key=lambda kv: -len(kv[0])):
            line = line.replace("$" + k, v)
        if not line.lower().startswith("coop "):
            return self.labs[who].ask(line)
        path = os.path.join(GAME, LOGS[who])
        start = os.path.getsize(path) if os.path.isfile(path) else 0
        got = self.labs[who].ask(line)
        out, last, end = [], -1, time.time() + 3
        while time.time() < end:
            time.sleep(0.3)
            with open(path, "rb") as f:
                f.seek(start)
                out = [l.split("Command: ", 1)[1].strip() for l in f.read().decode("utf-8", "replace").splitlines()
                       if "Command: " in l]
            if out and len(out) == last:
                break
            last = len(out)
        return " | ".join(out) if out else got

    def connect(self, n: int) -> bool:
        """The host hosts (by IP too) with Autotest Host; the friend joins the last IP address (127.0.0.1), loads
        Autotest Friend if asked to pick a save, and the host lets them in."""
        host, friend = self.labs["host"], self.labs["friend"]
        if "co-op hosting" in host.ask("status"):
            self.log("host: already hosting")
            return self.join(n)
        self.log(f"host> menu host => {host.ask('menu host')}")
        time.sleep(1)
        self.log(f"host> pick 1 => {host.ask('pick 1')}")
        time.sleep(1)
        load_host = 'load "' + SAVE_NAMES["host"] + '"'
        self.log(f"host> {load_host} => {host.ask(load_host)}")
        end = time.time() + 180
        while "co-op hosting" not in (got := host.ask("status")) or "world loaded" not in got:
            if time.time() >= end or self.crashed():
                self.bad(n, "connect: the host hosts with its save", got)
                return False
            time.sleep(2)
        self.ok(n, "connect: the host hosts with its save", got)
        return self.join(n)

    def join(self, n: int) -> bool:
        host, friend = self.labs["host"], self.labs["friend"]
        self.mark()
        self.log(f"friend> menu join => {friend.ask('menu join')}")
        time.sleep(1)
        got = friend.ask("pick 2")
        self.log(f"friend> pick 2 => {got}")
        if "last IP" not in got:
            self.bad(n, "connect: the friend picks the last IP address", got)
            return False
        loaded = False
        end = time.time() + 240
        while True:
            got = friend.ask("status")
            if "co-op joined" in got:
                break
            if time.time() >= end or self.crashed():
                self.bad(n, "connect: the friend joins", got)
                return False
            if not loaded and "picks one" in self.log_since_mark("friend"):
                load_friend = 'load "' + SAVE_NAMES["friend"] + '"'
                self.log(f"friend> {load_friend} => {friend.ask(load_friend)}")
                loaded = True
            if "answered" in (a := host.ask("approve yes")):
                self.log(f"host> approve yes => {a}")
            time.sleep(2)
        self.ok(n, "connect: the friend joins", got)
        return True

    # ---- the copies' logs (NVCoop.log, NVCoop-2.log), from the last mark on

    def mark(self) -> None:
        self.marks = {w: (os.path.getsize(os.path.join(GAME, n)) if os.path.isfile(os.path.join(GAME, n)) else 0)
                      for w, n in LOGS.items()}

    def log_since_mark(self, who: str) -> str:
        path = os.path.join(GAME, LOGS[who])
        try:
            with open(path, "rb") as f:
                f.seek(getattr(self, "marks", {}).get(who, 0))
                return f.read().decode("utf-8", errors="replace")
        except OSError:
            return ""

    def position_of(self, who: str, player: str) -> tuple[float, float] | None:
        """Where `who` sees player N (from status: '; player N at x y z')."""
        got = self.labs[who].ask("status")
        key = f"player {player} at "
        if key not in got:
            return None
        try:
            x, y = got.split(key, 1)[1].split()[:2]
            return float(x), float(y.rstrip(";"))
        except ValueError:
            return None

    def shot(self, who: str, name: str) -> None:
        pid = self.pids.get(who)
        hwnd = window_of(pid) if pid else 0
        safe = "".join(ch if ch.isalnum() or ch in "-_." else "_" for ch in name.strip() or "shot")
        out = os.path.join(self.dir, f"{len(self.shots) + 1:02d}-{who}-{safe}.png")
        if hwnd and grab(out, hwnd):
            self.shots.append(out)
            self.log(f"screenshot {os.path.basename(out)}")
        else:
            self.log(f"screenshot {who} {name} failed (no window, or minimised)")

    def quit(self) -> None:
        for who in ("friend", "host"):
            if who in self.pids and self.pids[who] in game_pids():
                self.log(f"{who}: {self.labs[who].ask('quitgame')}")
        for _ in range(30):
            if not any(p in game_pids() for p in self.launched):
                break
            time.sleep(1)
        for p in self.launched:
            if p in game_pids():
                self.log(f"pid {p} didn't quit: ending it")
                subprocess.run(["taskkill", "/PID", str(p), "/F"], capture_output=True)

    def step(self, n: int, line: str) -> None:
        s = line.strip()
        if not s or s.startswith("#"):
            return
        if not s.startswith("capture "):
            for k, v in sorted(self.vars.items(), key=lambda kv: -len(kv[0])):
                s = s.replace("$" + k, v)
        w = s.split()
        c = w[0]
        if c == "say":
            self.log("NOTE " + s[4:])
        elif c == "wait":
            time.sleep(float(w[1]) if len(w) > 1 else 1)
        elif c == "launch":
            if not self.launch():
                raise RuntimeError("launch failed")
        elif c == "connect":
            if not self.connect(n):
                raise RuntimeError("connect failed")
        elif c == "quit":
            self.quit()
        elif c == "shot" and len(w) >= 2:
            for who in whos(w[1]):
                self.shot(who, " ".join(w[2:]))
        elif c in ("expect", "waitfor") and len(w) >= 3:
            # expect <who> <line> <op> <want> / waitfor <who> <secs> <line> <op> <want>
            # ops: ~ (contains), !~ (doesn't), == (exactly), >= <= (the answer's first number)
            who = w[1]
            body = s.split(None, 2 if c == "expect" else 3)[-1]
            secs = float(w[2]) if c == "waitfor" else 0
            op = next((o for o in (" !~ ", " ~ ", " == ", " >= ", " <= ") if o in body), None)
            if not op:
                self.bad(n, s, "no ~, !~, ==, >= or <= in the step")
                return
            lab_line, _, want = body.partition(op)
            want = want.strip()
            for each in whos(who):
                end = time.time() + secs
                while True:
                    got = self.ask(each, lab_line.strip())
                    hit = check(got, op.strip(), want)
                    if hit or time.time() >= end or self.crashed():
                        break
                    time.sleep(1)
                (self.ok if hit else self.bad)(n, f"{each}: {lab_line.strip()} {op.strip()} {want}", got)
        elif c == "same" and len(w) >= 2:
            # same <line>: both copies answer the same (numbers rounded to whole ones)
            line_ = s.split(None, 1)[1]
            line_, _, word = line_.partition(" ~ ")
            a, b = self.ask("host", line_), self.ask("friend", line_)
            if word:
                # same <line> ~ <word>: as many parts of the answer mention <word> on both copies
                ca = sum(word.strip() in p for p in a.split(" | "))
                cb = sum(word.strip() in p for p in b.split(" | "))
                (self.ok if ca == cb else self.bad)(n, f"same count of '{word.strip()}': {line_}", f"host {ca} / friend {cb}")
            else:
                (self.ok if norm(a) == norm(b) else self.bad)(n, f"same on both: {line_}", f"host {a} / friend {b}")
        elif c == "capture" and len(w) >= 5:
            # capture <name> <who> <line> ~ <regex with one group>: $name in later steps
            import re
            name, who = w[1], w[2]
            body = s.split(None, 3)[3]
            line_, _, rx = body.partition(" ~ ")
            got = self.ask(who, line_.strip())
            m = re.search(rx.strip(), got)
            if m:
                self.vars[name] = m.group(1)
                self.log(f"${name} = {m.group(1)}")
            else:
                self.bad(n, f"capture {name}: {rx.strip()}", got[:300])
        elif c == "kill" and len(w) >= 2:
            pid = self.pids.pop(w[1], None)
            self.labs[w[1]].close()
            if pid:
                subprocess.run(["taskkill", "/PID", str(pid), "/F"], capture_output=True)
            self.log(f"killed {w[1]} (pid {pid}) on purpose")
            time.sleep(2)
        elif c == "relaunch" and len(w) >= 2:
            if not self.launch_one(w[1]):
                raise RuntimeError(f"relaunch {w[1]} failed")
        elif c == "mark":
            self.mark()
        elif c in ("waitlog", "nolog") and len(w) >= 3:
            # waitlog <who> <secs> <text> / nolog <who> <text>: in that copy's log since the last mark
            who = w[1]
            secs = float(w[2]) if c == "waitlog" else 0
            text = s.split(None, 3 if c == "waitlog" else 2)[-1]
            for each in whos(who):
                end = time.time() + secs
                while True:
                    found = text in self.log_since_mark(each)
                    if found or time.time() >= end or self.crashed():
                        break
                    time.sleep(0.5)
                if c == "waitlog":
                    (self.ok if found else self.bad)(n, f"{each} log has: {text}", "found" if found else "not in time")
                else:
                    (self.bad if found else self.ok)(n, f"{each} log lacks: {text}", "it is there" if found else "not there")
        elif c == "waitpos" and len(w) >= 7:
            # waitpos <who> <player id> <x> <y> <tolerance> <secs>: who sees that player there
            who, player, x, y, tol, secs = w[1], w[2], float(w[3]), float(w[4]), float(w[5]), float(w[6])
            end = time.time() + secs
            while True:
                at = self.position_of(who, player)
                close = at is not None and ((at[0] - x) ** 2 + (at[1] - y) ** 2) ** 0.5 <= tol
                if close or time.time() >= end or self.crashed():
                    break
                time.sleep(0.5)
            seen = f"at {at[0]:.0f} {at[1]:.0f}" if at else "not seen"
            (self.ok if close else self.bad)(n, f"{who} sees player {player} within {tol:.0f} of {x:.0f} {y:.0f}", seen)
        elif c == "focus" and len(w) >= 2:
            self.log(f"focus {w[1]}: {focus(window_of(self.pids.get(w[1], 0)))}")
        elif c.endswith(":") and c[:-1] in ("host", "friend", "both"):
            text = s[len(c):].strip()
            for each in whos(c[:-1]):
                self.log(f"{each}> {text}  =>  {self.ask(each, text)}")
        else:
            self.bad(n, "unknown step: " + s, "skipped")

    def go(self) -> int:
        with open(os.path.join(self.dir, "test.txt"), "w", encoding="utf-8") as f:
            f.write("\n".join(self.lines) + "\n")
        try:
            for i, line in enumerate(self.lines, 1):
                if time.time() - self.started > self.args.timeout:
                    self.outcome = f"timed out after {self.args.timeout:.0f} s"
                    break
                gone = self.crashed()
                if gone:
                    self.outcome = f"game closed or crashed: {', '.join(gone)}"
                    break
                self.step(i, line)
            else:
                self.outcome = "done"
        except RuntimeError as e:
            self.outcome = str(e)
        finally:
            for lab in self.labs.values():
                lab.close()
            if self.args.quit or self.outcome != "done":
                if not self.args.keep_open:
                    self.quit()
        return self.finish()

    def finish(self) -> int:
        gone = [w for w in self.pids if "crashed" in self.outcome and w in self.outcome]
        for who, name in LOGS.items():
            src = os.path.join(GAME, name)
            if os.path.isfile(src):
                shutil.copy2(src, os.path.join(self.dir, name))
        crash = os.path.join(GAME, "CrashLogger.log")
        if gone and os.path.isfile(crash):
            shutil.copy2(crash, os.path.join(self.dir, "CrashLogger.log"))
        removed = self.clean_saves()
        if len(self.shots) > 1:
            self.sheet()
        built, deployed = sha(BUILT_DLL), sha(GAME_DLL)
        with open(os.path.join(self.dir, "report.md"), "w", encoding="utf-8") as f:
            f.write(f"# Co-op autotest {os.path.basename(self.dir)}\n\n")
            f.write(f"- outcome: **{self.outcome}**\n")
            f.write(f"- game's NVCoop.dll {'matches' if built == deployed else 'DIFFERS from'} the build\n")
            f.write(f"- passed {len(self.passed)}, failed {len(self.failed)}, screenshots {len(self.shots)}, "
                    f"test saves removed {removed}\n\n")
            if self.failed:
                f.write("## Failed\n\n" + "".join(f"- `{l}`\n" for l in self.failed) + "\n")
            f.write("## Passed\n\n" + "".join(f"- `{l}`\n" for l in self.passed) + "\n")
            f.write("## Log\n\n```\n" + "\n".join(self.log_lines) + "\n```\n")
        print(f"\n{self.outcome}: passed {len(self.passed)}, failed {len(self.failed)} -> {self.dir}")
        return 0 if self.outcome == "done" and not self.failed else 1

    def clean_saves(self) -> int:
        """Deletes the saves this run wrote (files that weren't there before it), never the test saves."""
        folder = save_folder()
        if not os.path.isdir(folder) or any(p in game_pids() for p in self.launched):
            return 0
        keep = {n.lower() for base in SAVE_NAMES.values() for n in (base + ".fos", base + ".nvse")}
        n = 0
        for name in set(os.listdir(folder)) - self.saves_before:
            p = os.path.join(folder, name)
            if os.path.isfile(p) and name.lower() not in keep and name.lower().endswith((".fos", ".nvse", ".nvcoopid")):
                os.remove(p)
                self.log(f"removed test save file {name}")
                n += 1
        return n

    def sheet(self) -> None:
        n = len(self.shots)
        cols = min(2, n)
        args = [ffmpeg(), "-hide_banner", "-loglevel", "error", "-y"]
        for s in self.shots:
            args += ["-i", s]
        scaled = "".join(f"[{i}:v]scale=640:360,setsar=1[s{i}];" for i in range(n))
        pads = "".join(f"[s{i}]" for i in range(n))
        layout = "|".join(f"{(i % cols) * 640}_{(i // cols) * 360}" for i in range(n))
        graph = f"{scaled}{pads}xstack=inputs={n}:layout={layout}:fill=black[o]"
        try:
            subprocess.run(args + ["-filter_complex", graph, "-map", "[o]", "-frames:v", "1",
                                   os.path.join(self.dir, "shots.png")], timeout=60, check=True)
        except Exception as e:  # noqa: BLE001
            self.log(f"contact sheet failed: {e}")


# ---- commands

def cmd_run(args) -> int:
    built, deployed = sha(BUILT_DLL), sha(GAME_DLL)
    if built != deployed:
        print("warning: the game's NVCoop.dll is NOT this build (deploy --yes)")
    return Run(args.tests, args).go()


def cmd_lab(args) -> int:
    lab = Lab(args.who)
    print(lab.ask(" ".join(args.line)))
    lab.close()
    return 0


def cmd_list(_args) -> int:
    for p in sorted(glob.glob(os.path.join(TESTS, "*.txt"))):
        print(f"{os.path.splitext(os.path.basename(p))[0]:<16} {describe(p)}")
    return 0


def cmd_check(_args) -> int:
    built, deployed = sha(BUILT_DLL), sha(GAME_DLL)
    print(f"built:   {BUILT_DLL}\nin game: {GAME_DLL}\n{'same file' if built and built == deployed else 'DIFFERENT'}")
    print(f"saves:   {save_folder()}")
    print(f"game running: {game_pids() or 'no'}")
    return 0 if built and built == deployed else 1


def cmd_saves(args) -> int:
    """Copies a save (the newest, or --from NAME) to the two test saves. Never overwrites a save that is there."""
    folder = save_folder()
    if args.source:
        src = os.path.join(folder, args.source)
    else:
        fos = [f for f in glob.glob(os.path.join(folder, "*.fos"))
               if os.path.splitext(os.path.basename(f))[0] not in SAVE_NAMES.values()]
        if not fos:
            print(f"no saves in {folder}")
            return 1
        src = os.path.splitext(max(fos, key=os.path.getmtime))[0]
    src = src[:-4] if src.lower().endswith(".fos") else src
    for name in SAVE_NAMES.values():
        dst = os.path.join(folder, name)
        if os.path.exists(dst + ".fos"):
            print(f"{name} is already there (left as it is)")
            continue
        for ext in (".fos", ".nvse"):
            if os.path.isfile(src + ext):
                shutil.copy2(src + ext, dst + ext)
        print(f"{name} <- {os.path.basename(src)}")
    return 0


MANIFEST = os.path.join(OUT, "deployed.json")


def cmd_deploy(args) -> int:
    """The build's NVCoop.dll and MCM page into the game, and an NVCoop.ini with LabChannel=1. Files already there
    (Vortex hard links) are moved aside, never written through; undeploy puts them back."""
    if not args.yes:
        print("deploy writes into the game folder: run it with --yes once the user has said so")
        return 2
    if game_pids():
        print("the game is running: close it first")
        return 1
    os.makedirs(OUT, exist_ok=True)
    m = {"created": [], "moved": {}}
    if os.path.isfile(MANIFEST):
        with open(MANIFEST, encoding="utf-8") as f:
            m = json.load(f)

    def place(dst: str, data: bytes) -> None:
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.exists(dst) and dst not in m["created"] and dst not in m["moved"]:
            os.replace(dst, dst + ".before-autotest")
            m["moved"][dst] = dst + ".before-autotest"
        elif not os.path.exists(dst) and dst not in m["moved"] and dst not in m["created"]:
            m["created"].append(dst)
        tmp = dst + ".autotest-new"
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, dst)

    with open(BUILT_DLL, "rb") as f:
        place(GAME_DLL, f.read())
    with open(os.path.join(ROOT, "data", "MCM", "NVCoop.json"), "rb") as f:
        place(os.path.join(GAME, "Data", "MCM", "NVCoop.json"), f.read())
    # the player's own ini (the original, if deployed before), with LabChannel=1 and AllowSecondCopy=1
    src_ini = m["moved"].get(GAME_INI, GAME_INI)
    with open(src_ini, "rb") as f:
        text = f.read().decode("utf-8", errors="replace")
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = [l for l in text.splitlines() if not l.strip().lower().startswith(("labchannel=", "allowsecondcopy="))]
    text = nl.join(lines).rstrip() + nl + "AllowSecondCopy=1" + nl + "LabChannel=1" + nl
    place(GAME_INI, text.encode("utf-8"))
    tex = os.path.join(ROOT, "data", "textures")
    for folder, _, files in os.walk(tex):
        for name in files:
            if name.lower().endswith(".dds"):
                src = os.path.join(folder, name)
                with open(src, "rb") as f:
                    place(os.path.join(GAME, "Data", "Textures", os.path.relpath(src, tex)), f.read())
    with open(MANIFEST, "w", encoding="utf-8") as f:
        json.dump(m, f, indent=1)
    print(f"deployed: {len(m['created'])} new files, {len(m['moved'])} moved aside (undo: undeploy)")
    return 0


def cmd_undeploy(_args) -> int:
    if not os.path.isfile(MANIFEST):
        print("nothing deployed by autotest")
        return 0
    if game_pids():
        print("close the game first")
        return 1
    with open(MANIFEST, encoding="utf-8") as f:
        m = json.load(f)
    for dst in m["created"]:
        if os.path.isfile(dst):
            os.remove(dst)
    for dst, aside in m["moved"].items():
        if os.path.exists(aside):
            if os.path.exists(dst):
                os.remove(dst)
            os.replace(aside, dst)
    os.remove(MANIFEST)
    print(f"removed {len(m['created'])} files, put back {len(m['moved'])}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("tests", nargs="+")
    r.add_argument("--quit", action="store_true", help="both copies quit at the end")
    r.add_argument("--keep-open", action="store_true", help="never end the game processes")
    r.add_argument("--timeout", type=float, default=900)
    lab = sub.add_parser("lab")
    lab.add_argument("who", choices=["host", "friend"])
    lab.add_argument("line", nargs="+")
    sub.add_parser("list")
    sub.add_parser("check")
    s = sub.add_parser("saves")
    s.add_argument("--from", dest="source")
    d = sub.add_parser("deploy")
    d.add_argument("--yes", action="store_true")
    sub.add_parser("undeploy")
    args = ap.parse_args()
    return {"run": cmd_run, "lab": cmd_lab, "list": cmd_list, "check": cmd_check, "saves": cmd_saves,
            "deploy": cmd_deploy, "undeploy": cmd_undeploy}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
