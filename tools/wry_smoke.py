#!/usr/bin/env python3
"""wry_smoke: an end-to-end test of the game, run offscreen.

    python3 tools/wry_smoke.py [PATH_TO_CLIENT_BINARY]

Launches the client with --offscreen (the Metal scenes rendering with no
window, so screenshots work; --headless is the render-free host, which
tools/wry_net_smoke.py drives) on a private control socket, plays a
short scripted session through the command interface (main menu, NEW
GAME, a write, steps, a screenshot), checks each reply, then quits and
requires a clean exit.  The binary defaults to the newest Debug build in
DerivedData, so the session runs under the scheme's sanitizers.  Exit
status 0 on success.  Standard library only.
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
        sys.exit("wry_smoke: no Debug build found; pass the binary's path")
    return max(candidates, key=os.path.getmtime)


class Game:

    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.buffer = b""
        self.next_id = 1

    def run(self, line):
        request = {"id": self.next_id, "line": line}
        self.next_id += 1
        self.sock.sendall(json.dumps(request).encode("utf-8") + b"\n")
        while b"\n" not in self.buffer:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("the game closed the control socket")
            self.buffer += chunk
        reply, self.buffer = self.buffer.split(b"\n", 1)
        reply = json.loads(reply)
        print(f"  {line!r:44} -> {reply['status']}: {reply['text'][:60]!r}")
        return reply

    def ok(self, line):
        reply = self.run(line)
        if reply["status"] != "ok":
            raise AssertionError(f"{line!r} failed: {reply['text']}")
        return reply["text"]


def main(argv):
    binary = argv[0] if argv else find_binary()
    workdir = tempfile.mkdtemp(prefix="wry-smoke-")
    # Unix socket paths are short (104 bytes); keep it under /tmp.
    socket_path = f"/tmp/wry-smoke-{os.getpid()}.sock"
    log_path = os.path.join(workdir, "client.log")
    print(f"wry_smoke: {binary}\n  log: {log_path}")
    with open(log_path, "wb") as log:
        process = subprocess.Popen([binary, "--offscreen", "--control-socket", socket_path],
                                   stdout=log, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 60
        while not os.path.exists(socket_path):
            if process.poll() is not None or time.monotonic() > deadline:
                raise AssertionError("the game did not open its control socket")
            time.sleep(0.1)
        game = Game(socket_path)

        game.ok("frame 300")                            # past the splash
        widgets = game.ok("widgets")
        assert 'Button "New Game"' in widgets, widgets
        game.ok('press "New Game"')
        game.ok("frame 60")
        game.ok("pause")
        game.ok("write Coordinate(1, 1) OPCODE::SWAP")
        assert game.ok("get Coordinate(1, 1)") == "null"     # not yet stepped
        game.ok("step 2")
        assert game.ok("get Coordinate(1, 1)") == "OPCODE::SWAP"
        assert game.run("hold SWAP")["status"] == "error"     # Term needs OPCODE::
        image = os.path.join(workdir, "frame.png")
        game.ok(f'screenshot "{image}"')
        with open(image, "rb") as f:
            assert f.read(8) == b"\x89PNG\r\n\x1a\n", "screenshot is not a PNG"
        history = game.ok("history 3")
        assert "screenshot" in history, history
        game.ok("quit")

        status = process.wait(timeout=60)
        assert status == 0, f"the game exited with status {status}"
        assert not os.path.exists(socket_path), "the socket file was left behind"
    except BaseException:
        if process.poll() is None:
            process.kill()
        print(f"wry_smoke: FAILED (log: {log_path})", file=sys.stderr)
        raise
    print("wry_smoke: passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
