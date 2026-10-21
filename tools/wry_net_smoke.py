#!/usr/bin/env python3
"""wry_net_smoke: two headless games over TCP, in lockstep.

    python3 tools/wry_net_smoke.py [PATH_TO_CLIENT_BINARY]

Launches a host and a joiner, each headless on its own control socket.
The host starts a game, pauses its clock and listens; the joiner starts
the same scenario and joins the host, replaying its stream.  Then each
side issues an action and the host steps the clock, and the script
requires both to report the same tile contents and the same state hash
after every round (game/docs/multiplayer.md section 13, stage 3).  The
binary defaults to the newest Debug build in DerivedData.  Exit status 0
on success.  Standard library only.
"""

import glob
import json
import os
import socket
import subprocess
import sys
import tempfile
import time


def find_binary():
    pattern = os.path.expanduser(
        "~/Library/Developer/Xcode/DerivedData/client-*/Build/Products/Debug/"
        "client.app/Contents/MacOS/client")
    candidates = glob.glob(pattern)
    if not candidates:
        sys.exit("wry_net_smoke: no Debug build found; pass the binary's path")
    return max(candidates, key=os.path.getmtime)


class Game:

    def __init__(self, name, path):
        self.name = name
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.buffer = b""
        self.next_id = 1

    def run(self, line, quiet=False):
        request = {"id": self.next_id, "line": line}
        self.next_id += 1
        self.sock.sendall(json.dumps(request).encode("utf-8") + b"\n")
        while b"\n" not in self.buffer:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError(f"{self.name} closed the control socket")
            self.buffer += chunk
        reply, self.buffer = self.buffer.split(b"\n", 1)
        reply = json.loads(reply)
        if not quiet:
            print(f"  {self.name:6} {line!r:44} -> {reply['status']}: {reply['text'][:60]!r}")
        return reply

    def ok(self, line, quiet=False):
        reply = self.run(line, quiet)
        if reply["status"] != "ok":
            raise AssertionError(f"{self.name}: {line!r} failed: {reply['text']}")
        return reply["text"]


class Process:

    def __init__(self, name, binary, workdir, extra=()):
        self.name = name
        self.socket_path = f"/tmp/wry-net-{name}-{os.getpid()}.sock"
        self.log_path = os.path.join(workdir, f"{name}.log")
        self.log = open(self.log_path, "wb")
        self.process = subprocess.Popen(
            [binary, "--headless", "--control-socket", self.socket_path, *extra],
            stdout=self.log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 60
        while not os.path.exists(self.socket_path):
            if self.process.poll() is not None or time.monotonic() > deadline:
                raise AssertionError(f"{name} did not open its control socket")
            time.sleep(0.1)
        self.game = Game(name, self.socket_path)

    def finish(self):
        status = self.process.wait(timeout=60)
        self.log.close()
        assert status == 0, f"{self.name} exited with status {status}"
        assert not os.path.exists(self.socket_path), f"{self.name} left its socket file behind"

    def kill(self):
        if self.process.poll() is None:
            self.process.kill()
        self.log.close()


def until(what, condition, tick=None, timeout=30):
    """Wait for `condition`, calling `tick` between checks: with the
    host's clock paused, a request that reaches the relay after a step
    was finalized rides the next one, so the host keeps stepping as its
    clock would."""
    deadline = time.monotonic() + timeout
    while True:
        if condition():
            return
        if time.monotonic() > deadline:
            raise AssertionError(f"timed out waiting for {what}")
        if tick:
            tick()
        time.sleep(0.1)


def main(argv):
    binary = argv[0] if argv else find_binary()
    workdir = tempfile.mkdtemp(prefix="wry-net-smoke-")
    print(f"wry_net_smoke: {binary}\n  logs: {workdir}")
    host = joiner = None
    try:
        host = Process("host", binary, workdir)
        h = host.game
        h.ok("frame 300")                               # past the splash
        h.ok('press "New Game"')
        h.ok("frame 30")
        h.ok("pause")
        port = int(h.ok("host 0"))
        assert port != 0

        joiner = Process("joiner", binary, workdir)
        j = joiner.game
        j.ok("frame 300")
        j.ok('press "New Game"')
        j.ok("frame 30")
        j.ok(f'join "127.0.0.1:{port}"')

        def level():
            return h.ok("state-hash", quiet=True) == j.ok("state-hash", quiet=True)

        def tick():
            h.ok("step 1", quiet=True)

        # The joiner replays the host's stream so far and its JOIN rides
        # a step
        h.ok("step 3")
        until("the joiner to catch up", level, tick)
        print(f"  level at {h.ok('state-hash')}")

        # The joiner acts; the host's clock carries it to both
        j.ok("write Coordinate(1, 1) OPCODE::SWAP")
        h.ok("step 3")
        until("the joiner's write to land on both",
              lambda: h.ok("get Coordinate(1, 1)", quiet=True) == "OPCODE::SWAP"
                      and j.ok("get Coordinate(1, 1)", quiet=True) == "OPCODE::SWAP",
              tick)
        until("level after the joiner's write", level, tick)

        # The host acts
        h.ok("write Coordinate(2, 2) OPCODE::SWAP")
        h.ok("step 3")
        until("the host's write to land on both",
              lambda: h.ok("get Coordinate(2, 2)", quiet=True) == "OPCODE::SWAP"
                      and j.ok("get Coordinate(2, 2)", quiet=True) == "OPCODE::SWAP",
              tick)
        until("level after the host's write", level, tick)
        print(f"  level at {h.ok('state-hash')}")

        # A joined client cannot command the clock; the joiner leaves and
        # plays on alone
        assert j.run("pause")["status"] == "error"
        j.ok("leave")
        j.ok("frame 10")
        j.ok("quit")
        h.ok("quit")
        joiner.finish()
        host.finish()
    except BaseException:
        for p in (host, joiner):
            if p is not None:
                p.kill()
        print(f"wry_net_smoke: FAILED (logs: {workdir})", file=sys.stderr)
        raise
    print("wry_net_smoke: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
