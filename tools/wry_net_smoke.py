#!/usr/bin/env python3
"""wry_net_smoke: two headless games over TCP, in lockstep.

    python3 tools/wry_net_smoke.py [PATH_TO_CLIENT_BINARY]

Launches a host and a joiner, each headless (the render-free host: no
window, device or AppKit) on its own control socket.
The host starts a game, pauses its clock, plays on for a while and
listens; the joiner starts its own game, abandons it to join the host,
receives the host's save of the world as of the step it arrived at, and
follows the stream from there.  Then each side issues an action and the
host steps the clock, and the script requires both to report the same
tile contents and the same state hash after every round
(game/docs/multiplayer.md section 13, stages 3 to 5).  Last, the
diagnosis harness (stage 6): the joiner's world is changed off the
stream, the structural diff of the two dumps names the cell, the relay
kicks the joiner at its next report, `compare` finds the step in the
two session logs, the host's log replays consistently through it and
the joiner's does not reproduce what it recorded.  The binary defaults
to the newest Debug build in DerivedData.  Exit status 0 on success.
Standard library only.
"""

import difflib
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


def game_environment():
    """The game's environment: a sanitizer finding exits after its report
    instead of aborting (macOS's default), so a failing run reports on
    stderr with no Problem Report dialog, as the game's own fatal-signal
    handler arranges for asserts in its driven modes; and the Swift
    backtracer's prompt is off."""
    env = dict(os.environ)
    for name in ("ASAN_OPTIONS", "UBSAN_OPTIONS", "TSAN_OPTIONS"):
        env[name] = ":".join(x for x in (env.get(name, ""), "abort_on_error=0") if x)
    env["SWIFT_BACKTRACE"] = "enable=no"
    return env


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
            stdout=self.log, stderr=subprocess.STDOUT, env=game_environment())
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
        h.ok("write Coordinate(5, 5) OPCODE::SWAP")       # something a joiner must inherit
        h.ok("step 40")                                   # well past genesis
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

        # The joiner receives the host's save as of the head, catches up on
        # the stream from there, and its JOIN rides a step once its hash
        # report has proved it
        h.ok("step 3")
        until("the joiner to catch up", level, tick)
        print(f"  level at {h.ok('state-hash')}")
        # Its JOIN rides the step after the report that opened the gate
        until("the joiner to be admitted",
              lambda: j.ok("session", quiet=True).startswith("joined joined"), tick)
        session = j.ok("session")
        assert session.startswith("joined joined client 2 head "), session
        assert int(session.split()[5]) >= 40, session
        assert j.ok("get Coordinate(5, 5)") == "OPCODE::SWAP"

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

        # Both sides have been reporting hashes after every step; a
        # disagreement would have had the joiner kicked by now
        assert h.ok("session").startswith("host joined client 1 "), h.ok("session")
        assert j.ok("session").startswith("joined joined client 2 "), j.ok("session")

        # A joined client cannot command the clock
        assert j.run("pause")["status"] == "error"

        # --- The diagnosis harness (multiplayer.md section 5) ---
        # Each session has been writing its log beside the saves
        def log_of(game):
            session = game.ok("session", quiet=True)
            assert " log " in session, session
            return session.split(" log ", 1)[1]
        host_log = log_of(h)
        joiner_log = log_of(j)
        assert host_log != joiner_log
        print(f"  logs: {host_log} {joiner_log}")

        # Level, the two worlds dump identically; the joiner's world
        # changed off the stream, the structural diff names the cell
        before = h.ok("dump", quiet=True).splitlines()
        assert before == j.ok("dump", quiet=True).splitlines()
        j.ok("corrupt Coordinate(3, 3) OPCODE::SWAP")
        after = j.ok("dump", quiet=True).splitlines()
        changed = [line for line in difflib.unified_diff(before, after, lineterm="", n=0)
                   if line[:1] in "+-" and not line.startswith(("+++", "---"))]
        assert "+    Coordinate(3, 3): OPCODE::SWAP" in changed, changed
        assert all(("Coordinate(3, 3)" in line) or ("_term_for_coordinate:" in line)
                   for line in changed), changed
        print(f"  dump diff: {changed}")

        # The joiner's next report disagrees with the reference's: kicked
        until("the joiner to be kicked for the desync",
              lambda: "desync at step" in j.ok("session", quiet=True), tick)
        session = j.ok("session")
        desync = int(session.split("desync at step ", 1)[1].split()[0])

        # Offline: the two logs name the step
        report = j.ok(f'compare "{host_log}" "{joiner_log}"')
        assert "'s checkpoint matches " in report, report
        assert "streams identical" in report, report
        assert f"hashes differ at step {desync}" in report, report

        # The kicked joiner plays on alone from where it is: a fork
        j.ok("leave")
        assert j.ok("session").startswith("host "), j.ok("session")

        # The host's log replays consistently on this build, through the
        # divergent step; the joiner's own log does not reproduce what it
        # recorded there, since the change came from outside the stream
        reply = j.ok(f'replay "{host_log}" {desync}')
        assert reply.endswith(": consistent"), reply
        reply = j.ok(f'replay "{joiner_log}" {desync}')
        assert reply.startswith(f"diverged at step {desync}:"), reply
        assert j.ok("session").startswith("replay of "), j.ok("session")

        # A replayed world is played on by hosting it
        j.ok("host 0")
        assert j.ok("session").startswith("host "), j.ok("session")
        j.ok("frame 10")
        j.ok("quit")
        h.ok("quit")
        joiner.finish()
        host.finish()
    except BaseException:
        # What each side thought was going on, while it can still say
        for p in (host, joiner):
            if p is not None and p.process.poll() is None:
                try:
                    print(f"  {p.name} session: {p.game.run('session', quiet=True)['text']}", file=sys.stderr)
                except Exception as e:
                    print(f"  {p.name}: no answer ({e})", file=sys.stderr)
        for p in (host, joiner):
            if p is not None:
                p.kill()
        print(f"wry_net_smoke: FAILED (logs: {workdir})", file=sys.stderr)
        raise
    print("wry_net_smoke: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
