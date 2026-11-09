#!/usr/bin/env python3
"""Boots dmaOS in QEMU and runs the system tests.

Each test session boots a fresh VM, types shell commands on the serial console
and waits for the kernel's process-exit messages. The shell's own output goes
to the framebuffer, so the Lua test scripts write their results to the data
disk (mounted at /home), which is read back from the image afterwards.

Run through `make test`, which builds the test disk images and passes the
paths below. Uses only the Python standard library.
"""

import argparse
import http.server
import json
import os
import re
import select
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

# ---------------------------------------------------------------------------
# Reading files back from a dmaOS disk image (see shared/fs_shared.h)
# ---------------------------------------------------------------------------

BSIZE = 1024
DINODE_SIZE = 128
NDIRECT = 12
DIRENT_SIZE = 32
FT_DIRECTORY = 1


class DiskImage:
    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        (self.magic, _, _, self.ninodes, self.inodestart,
         _) = struct.unpack_from("<6I", self.data, BSIZE)

    def _inode(self, inum):
        off = self.inodestart * BSIZE + inum * DINODE_SIZE
        itype, _, _, _, size = struct.unpack_from("<4HI", self.data, off)
        addrs = struct.unpack_from(f"<{NDIRECT}I", self.data, off + 12)
        return itype, size, addrs

    def _contents(self, inum):
        _, size, addrs = self._inode(inum)
        nblocks = (size + BSIZE - 1) // BSIZE
        if nblocks > NDIRECT:
            raise ValueError("test output files must fit in direct blocks")
        raw = b"".join(self.data[a * BSIZE:(a + 1) * BSIZE]
                       for a in addrs[:nblocks])
        return raw[:size]

    def _lookup(self, path):
        """Returns the inode number of path, or None if missing."""
        inum = 1
        for name in [p for p in path.split("/") if p]:
            itype, _, _ = self._inode(inum)
            if itype != FT_DIRECTORY:
                return None
            entries = self._contents(inum)
            for off in range(0, len(entries), DIRENT_SIZE):
                (child,) = struct.unpack_from("<H", entries, off)
                ename = entries[off + 2:off + DIRENT_SIZE].split(b"\0")[0]
                if child and ename.decode() == name:
                    inum = child
                    break
            else:
                return None
        return inum

    def exists(self, path):
        return self._lookup(path) is not None

    def read(self, path):
        """Returns the contents of the file at path, or None if missing."""
        inum = self._lookup(path)
        return None if inum is None else self._contents(inum)


# ---------------------------------------------------------------------------
# Driving a VM
# ---------------------------------------------------------------------------


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class VM:
    def __init__(self, args, name, hostfwd=None):
        self.name = name
        self.log_path = os.path.join(args.workdir, f"{name}.serial.log")
        self.log = open(self.log_path, "w")
        self.serial = ""
        netdev = "user,id=net0"
        if hostfwd:
            netdev += "," + hostfwd
        # QMP sends keys to the virtio keyboard. Unix socket paths are short,
        # so it lives in a temporary directory rather than the workdir.
        self.qmp_dir = tempfile.mkdtemp(prefix="dmaos")
        qmp_path = os.path.join(self.qmp_dir, "qmp.sock")
        cmd = [
            args.qemu, "-machine", "virt", "-bios", args.bios,
            "-nographic", "-serial", "mon:stdio", "--no-reboot",
            "-display", "none",
            "-fw_cfg", "name=opt/dmaos/width,string=1024",
            "-fw_cfg", "name=opt/dmaos/height,string=768",
            "-drive", f"id=drive0,file={args.disk},format=raw,if=none",
            "-device", "virtio-blk-device,drive=drive0,bus=virtio-mmio-bus.0",
            "-drive", f"id=drive1,file={args.data},format=raw,if=none",
            "-device", "virtio-blk-device,drive=drive1,bus=virtio-mmio-bus.1",
            "-device", "ramfb",
            "-device", "virtio-keyboard-device,bus=virtio-mmio-bus.2",
            "-netdev", netdev,
            "-device", "virtio-net-device,netdev=net0,bus=virtio-mmio-bus.3",
            "-kernel", args.kernel,
            "-qmp", f"unix:{qmp_path},server,wait=off",
        ]
        self.proc = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT)
        self.qmp = None
        end = time.time() + 10
        while self.qmp is None and time.time() < end:
            try:
                sock = socket.socket(socket.AF_UNIX)
                sock.connect(qmp_path)
                self.qmp = sock.makefile("rw")
            except OSError:
                time.sleep(0.05)
        if self.qmp is None:
            raise RuntimeError("could not connect to QMP")
        self.qmp.readline()  # greeting
        self._qmp("qmp_capabilities")

    def _qmp(self, command, **arguments):
        msg = {"execute": command}
        if arguments:
            msg["arguments"] = arguments
        self.qmp.write(json.dumps(msg) + "\n")
        self.qmp.flush()
        while True:
            reply = json.loads(self.qmp.readline())
            if "return" in reply or "error" in reply:
                return reply

    def send_key(self, *qcodes):
        """Presses the keys together on the virtio keyboard, e.g. ctrl, c."""
        keys = [{"type": "qcode", "data": k} for k in qcodes]
        self._qmp("send-key", keys=keys)

    def send(self, data):
        """Writes raw bytes to the serial console."""
        self.proc.stdin.write(data)
        self.proc.stdin.flush()
        self.pump(0.05)

    def exits(self):
        """Returns the exit codes logged so far, in order."""
        return [int(code) for code in
                re.findall(r"Process exited: PID=\d+ with exit code (-?\d+)",
                           self.serial)]

    def wait_exits(self, before, count, timeout):
        """Waits for count exits after the first `before`; returns their
        codes."""
        end = time.time() + timeout
        while len(self.exits()) < before + count and time.time() < end:
            if self.proc.poll() is not None:
                break
            self.pump(0.1)
        return self.exits()[before:]

    def pump(self, seconds):
        """Collects serial output for up to the given time."""
        end = time.time() + seconds
        while time.time() < end and self.proc.poll() is None:
            ready, _, _ = select.select([self.proc.stdout], [], [], 0.05)
            if ready:
                chunk = os.read(self.proc.stdout.fileno(), 4096)
                if not chunk:
                    break
                text = chunk.decode(errors="replace")
                self.serial += text
                self.log.write(text)
                self.log.flush()

    def wait_for(self, pattern, timeout, count=1):
        """Waits until pattern has appeared count times in the serial log."""
        end = time.time() + timeout
        while time.time() < end:
            if len(re.findall(pattern, self.serial)) >= count:
                return True
            if self.proc.poll() is not None:
                break
            self.pump(0.1)
        return len(re.findall(pattern, self.serial)) >= count

    def boot(self):
        if not self.wait_for(r"Spawning shell process", 30):
            raise RuntimeError("the shell was not spawned")
        self.pump(1.0)

    def run(self, command, processes=1, timeout=60):
        """Types a shell command and waits for its processes to exit."""
        before = self.serial.count("Process exited")
        for ch in command + "\r":
            self.proc.stdin.write(ch.encode())
            self.proc.stdin.flush()
            self.pump(0.01)
        return self.wait_for(r"Process exited", timeout,
                             count=before + processes)

    def type(self, command, enter=True):
        for ch in command + ("\r" if enter else ""):
            self.proc.stdin.write(ch.encode())
            self.proc.stdin.flush()
            self.pump(0.01)

    def wait_exit(self, timeout):
        end = time.time() + timeout
        while time.time() < end and self.proc.poll() is None:
            self.pump(0.1)
        return self.proc.poll() is not None

    def kill(self):
        if self.qmp is not None:
            self.qmp.close()
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()
        self.pump(0)
        self.log.close()
        shutil.rmtree(self.qmp_dir, ignore_errors=True)


# ---------------------------------------------------------------------------
# Test sessions
# ---------------------------------------------------------------------------


class Results:
    def __init__(self):
        self.failed = 0
        self.passed = 0

    def check(self, name, ok, detail=""):
        if ok:
            self.passed += 1
            print(f"  PASS  {name}")
        else:
            self.failed += 1
            print(f"  FAIL  {name}" + (f": {detail}" if detail else ""))

    def no_panic(self, vm):
        self.check(f"{vm.name}: no kernel panic", "WHOOPS" not in vm.serial,
                   f"see {vm.log_path}")


WGET_BODY = b"hello from the test host\n"

# How late a sleep may end on average. QEMU on macOS delivers timer interrupts
# about 1-1.5 ms late (its main loop waits with millisecond timeouts), so allow
# for that. Sleeps that only woke on 10 ms scheduler ticks would average about
# 5 ms late and fail.
SLEEP_SLACK_US = 3000


class WgetHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Length", str(len(WGET_BODY)))
        self.end_headers()
        self.wfile.write(WGET_BODY)

    def log_message(self, *args):
        pass


def session_core(args, results):
    """Preemption, blocking pipes, sleeps, a TCP connect and power off."""
    print("core: preemption, pipes, sleeps, TCP connect, idle and power off")
    server = http.server.HTTPServer(("127.0.0.1", 0), WgetHandler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]

    vm = VM(args, "core")
    try:
        vm.boot()
        # count.lua must finish while spin.lua is still spinning.
        results.check(
            "fairness pipeline completes",
            vm.run("lua /tests/spin.lua 8 /home/spin.out | "
                   "lua /tests/count.lua /home/count.out", processes=2))
        results.check(
            "20 KB through a 512-byte pipe completes",
            vm.run("lua /tests/gen.lua | lua /tests/sum.lua /home/pipe.out",
                   processes=2))
        results.check("sleeptest completes",
                      vm.run("/tests/sleeptest /home/sleep.out"))
        # QEMU's user network maps 10.0.2.2 to the host's loopback.
        results.check(
            "wget from the host completes",
            vm.run(f"wget -O /home/wget.out http://10.0.2.2:{port}/"))
        vm.pump(3.0)  # idle at the prompt
        vm.type("exit")
        results.check("exit powers off", vm.wait_exit(15))
    finally:
        vm.kill()
        server.shutdown()

    results.no_panic(vm)
    disk = DiskImage(args.data)

    spin, count = disk.read("/spin.out"), disk.read("/count.out")
    if spin is None or count is None:
        results.check("fairness: count ran while spin was spinning", False,
                      "missing output")
    else:
        spin_end = int(spin.split()[0])
        counts = [line.split() for line in count.decode().splitlines()]
        count_end = int(counts[-1][1]) + 1
        results.check(
            "fairness: count ran while spin was spinning",
            [c[0] for c in counts] == ["1", "2", "3", "4", "5"]
            and count_end < spin_end,
            f"count finished at {count_end}, spin at {spin_end}")

    pipe = disk.read("/pipe.out")
    results.check("pipe: every byte arrived",
                  pipe == b"2000 22000 line  2000\n", repr(pipe))

    # Sleeps must never end early, and must end close to their deadline rather
    # than on the next 10 ms scheduler tick.
    sleep = disk.read("/sleep.out")
    if sleep is None:
        results.check("sleep accuracy", False, "missing output")
    else:
        for line in sleep.decode().splitlines():
            req, lo, avg, hi = (int(x) for x in line.split())
            results.check(
                f"sleep {req // 1000} ms: never early, average within "
                f"{SLEEP_SLACK_US} us",
                lo >= req and avg <= req + SLEEP_SLACK_US,
                f"min {lo} us, avg {avg} us, max {hi} us")

    wget = disk.read("/wget.out")
    results.check("wget: body matches", wget == WGET_BODY, repr(wget))

    m = re.search(r"Powering off after (\d+) timer ticks in (\d+) ms",
                  vm.serial)
    if m:
        hz = int(m.group(1)) / (int(m.group(2)) / 1000)
        results.check("timer ticks at about 100 Hz", 85 <= hz <= 110,
                      f"{hz:.1f} Hz")
    else:
        results.check("timer ticks at about 100 Hz", False,
                      "no power-off message")


def session_httpd(args, results):
    """Serving HTTP from the guest to the host."""
    print("httpd: serving HTTP to the host")
    port = free_port()
    vm = VM(args, "httpd", hostfwd=f"hostfwd=tcp:127.0.0.1:{port}-:8080")
    try:
        vm.boot()
        vm.type("lua /httpd.lua")
        bodies = []
        end = time.time() + 30
        while time.time() < end and len(bodies) < 3:
            vm.pump(0.2)
            try:
                url = f"http://127.0.0.1:{port}/"
                with urllib.request.urlopen(url, timeout=5) as r:
                    if r.status == 200:
                        bodies.append(r.read())
            except OSError:
                pass
        results.check("three requests answered", len(bodies) == 3,
                      f"{len(bodies)} answered")
        results.check("index page served",
                      bool(bodies) and b"<html" in bodies[0].lower())
    finally:
        vm.kill()
    results.no_panic(vm)


SIGINT_EXIT = 128 + 2
SIGQUIT_EXIT = 128 + 3


def session_signals(args, results):
    """Ctrl+C and Ctrl+\\ from the serial console and the keyboard."""
    print("signals: Ctrl+C and Ctrl+\\")
    vm = VM(args, "signals")
    try:
        vm.boot()

        before = len(vm.exits())
        vm.type("lua /tests/spin.lua 30 /home/spin1.out")
        vm.pump(2.0)
        vm.send(b"\x03")
        codes = vm.wait_exits(before, 1, 5)
        results.check("serial Ctrl+C ends a spinning process",
                      codes == [SIGINT_EXIT], f"exit codes {codes}")

        before = len(vm.exits())
        vm.type("lua /tests/spin.lua 30 /home/spin2.out | "
                "lua /tests/spin.lua 30 /home/spin3.out")
        vm.pump(2.0)
        vm.send(b"\x03")
        codes = vm.wait_exits(before, 2, 5)
        results.check("serial Ctrl+C ends every process in a pipeline",
                      codes == [SIGINT_EXIT, SIGINT_EXIT],
                      f"exit codes {codes}")

        # sum.lua blocks reading the console, so this checks that a blocked
        # process is woken and ended.
        before = len(vm.exits())
        vm.type("lua /tests/sum.lua /home/sum.out")
        vm.pump(2.0)
        vm.send(b"\x1c")
        codes = vm.wait_exits(before, 1, 5)
        results.check("serial Ctrl+\\ ends a process blocked reading input",
                      codes == [SIGQUIT_EXIT], f"exit codes {codes}")

        before = len(vm.exits())
        vm.type("lua /tests/spin.lua 30 /home/spin4.out")
        vm.pump(2.0)
        vm.send_key("ctrl", "c")
        codes = vm.wait_exits(before, 1, 5)
        results.check("keyboard Ctrl+C ends a spinning process",
                      codes == [SIGINT_EXIT], f"exit codes {codes}")

        # rawkey turns ISIG off and exits without restoring it.
        before = len(vm.exits())
        vm.type("/tests/rawkey /home/rawkey.out")
        vm.pump(2.0)
        vm.send(b"\x03")
        codes = vm.wait_exits(before, 1, 5)
        results.check("with ISIG off, Ctrl+C is read as a character",
                      codes == [0], f"exit codes {codes}")

        before = len(vm.exits())
        vm.type("lua /tests/spin.lua 30 /home/spin5.out")
        vm.pump(2.0)
        vm.send(b"\x03")
        codes = vm.wait_exits(before, 1, 5)
        results.check("ISIG is back on after the process that cleared it exits",
                      codes == [SIGINT_EXIT], f"exit codes {codes}")

        # With no job running, the shell gets Ctrl+C and discards the line.
        vm.type("lua /tests/spin.lua 0 /home/discarded.out", enter=False)
        vm.send(b"\x03")
        results.check("shell survives Ctrl+C at the prompt",
                      vm.run("lua /tests/spin.lua 0 /home/kept.out"))

        vm.type("exit")
        vm.wait_exit(15)
    finally:
        vm.kill()
    results.no_panic(vm)

    disk = DiskImage(args.data)
    results.check("interrupted processes didn't finish",
                  not any(disk.exists(f"/spin{i}.out") for i in range(1, 6)))
    rawkey = disk.read("/rawkey.out")
    results.check("rawkey read byte 3", rawkey == b"3\n", repr(rawkey))
    results.check("shell discarded the line typed before Ctrl+C",
                  not disk.exists("/discarded.out")
                  and disk.exists("/kept.out"))


def session_doom(args, results):
    """DOOM keeps running under the timer and owns the keyboard."""
    print("doom: runs, owns the keyboard, serial Ctrl+C ends it")
    if not DiskImage(args.disk).exists("/doom1.wad"):
        print("  SKIP  no /doom1.wad on the root disk (see disk/*.wad)")
        return
    vm = VM(args, "doom")
    try:
        vm.boot()
        before = len(vm.exits())
        vm.type("doom")
        vm.pump(10.0)
        results.check("doom still running after 10 s",
                      len(vm.exits()) == before and vm.proc.poll() is None)

        # DOOM reads raw key events, so keyboard Ctrl+C is just two keys.
        vm.send_key("ctrl", "c")
        vm.pump(2.0)
        results.check("keyboard Ctrl+C doesn't end doom",
                      len(vm.exits()) == before)

        vm.send(b"\x03")
        codes = vm.wait_exits(before, 1, 5)
        results.check("serial Ctrl+C ends doom", codes == [SIGINT_EXIT],
                      f"exit codes {codes}")
    finally:
        vm.kill()
    results.no_panic(vm)


SESSIONS = {"core": session_core, "signals": session_signals,
            "httpd": session_httpd, "doom": session_doom}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--bios", default="default")
    parser.add_argument("--kernel", required=True)
    parser.add_argument("--disk", required=True)
    parser.add_argument("--data", required=True,
                        help="a scratch data disk; tests write to it")
    parser.add_argument("--workdir", required=True,
                        help="where serial logs are written")
    parser.add_argument("sessions", nargs="*",
                        help=f"sessions to run: {', '.join(SESSIONS)} "
                        "(default: all)")
    args = parser.parse_args()
    for name in args.sessions:
        if name not in SESSIONS:
            parser.error(f"unknown session '{name}'")

    results = Results()
    for name in args.sessions or SESSIONS:
        SESSIONS[name](args, results)

    print(f"\n{results.passed} passed, {results.failed} failed "
          f"(serial logs in {args.workdir})")
    sys.exit(1 if results.failed else 0)


if __name__ == "__main__":
    main()
