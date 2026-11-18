#!/usr/bin/env python3
"""measure_step_pump: the world's head against wall time under slow frames.

    tools/measure_step_pump.py BINARY LABEL OUTDIR
    tools/measure_step_pump.py --summarize CSV...

Launches BINARY --offscreen --no-tests --skip-splash with its own control
socket and settings directory, loads the save pt_reference, then runs
phases of frames (raster; `frame-stall` 250 and 1500 ms; `path-trace`),
sampling `session` back to back.  Each sample: wall time (s since the
phase began), the relay's head, the steps applied by the displayed World,
and the step pump's live count where the build has one
(docs/step_pump.md section 12).  Writes OUTDIR/LABEL.csv and prints a
summary per phase: the frame period (reply spacing), the relay's and the
world's steps per second, and the most the world and the displayed World
trailed the relay.

HAZARD: the load starts a session, whose log goes into the real
~/Desktop/assets/saves (kept ten deep): back the saves up first, and
move the new session-*.wrylog out afterwards.
"""

import csv
import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import time


class Connection:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.buffer = b""
        self.next_id = 1

    def run(self, line):
        payload = {"line": line, "id": self.next_id}
        self.next_id += 1
        self.sock.sendall(json.dumps(payload).encode() + b"\n")
        while b"\n" not in self.buffer:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("closed")
            self.buffer += chunk
        line, self.buffer = self.buffer.split(b"\n", 1)
        return json.loads(line)


def number(text, key):
    m = re.search(r"\b" + key + r" (\d+)", text)
    return int(m.group(1)) if m else None


def summarize(path):
    """The summary of a CSV this tool wrote.  `applied` and `pumped` count
    steps since the load, the relay's head is World time: the save's time
    is the relay's head less the count at the first raster sample, where
    the world is level"""
    rows = list(csv.DictReader(open(path)))
    label = os.path.basename(path)[:-4]
    first = rows[0]
    offset = int(first["relay"]) - int(first["pumped"] or first["applied"])
    print(f"{label}: offset {offset} (the save's time)")
    print(f"{label}: phase         samples frame_ms relay/s world/s"
          f" max(relay-world) max(relay-displayed)")
    for name in dict.fromkeys(r["phase"] for r in rows):
        ph = [r for r in rows if r["phase"] == name]
        if len(ph) < 2:
            continue
        t = [float(r["t_recv"]) for r in ph]
        relay = [int(r["relay"]) for r in ph]
        disp = [int(r["applied"]) + offset for r in ph]
        world = [int(r["pumped"]) + offset if r["pumped"] else d for r, d in zip(ph, disp)]
        dt = t[-1] - t[0]
        print(f"{label}: {name:13s} {len(ph):6d} {1000 * dt / (len(ph) - 1):8.1f}"
              f" {(relay[-1] - relay[0]) / dt:7.1f} {(world[-1] - world[0]) / dt:7.1f}"
              f" {max(r - w for r, w in zip(relay, world)):10d}"
              f" {max(r - d for r, d in zip(relay, disp)):12d}", flush=True)


def main(binary, label, outdir):
    work = tempfile.mkdtemp(prefix="wry-measure-")
    sock = os.path.join(work, "c.sock")
    settings = os.path.join(work, "settings")
    os.makedirs(settings)
    env = dict(os.environ, ASAN_OPTIONS="abort_on_error=0", SWIFT_BACKTRACE="enable=no")
    log = open(os.path.join(outdir, label + ".client.log"), "w")
    proc = subprocess.Popen([binary, "--offscreen", "--no-tests", "--skip-splash",
                             "--control-socket", sock, "--settings", settings],
                            stdout=log, stderr=subprocess.STDOUT, env=env)
    try:
        for _ in range(600):
            if os.path.exists(sock):
                break
            time.sleep(0.1)
        time.sleep(0.5)
        c = Connection(sock)

        def must(line):
            r = c.run(line)
            if r.get("status") != "ok":
                raise RuntimeError(f"{line}: {r}")
            return r.get("text", "")

        must('load "pt_reference"')
        must("frame 30")
        loaded = must("session")
        print(label, "loaded:", loaded, flush=True)

        phases = [
            ("raster", ["frame-stall 0", 'path-trace "off"'], 6.0),
            ("stall-250ms", ["frame-stall 250"], 8.0),
            ("stall-1500ms", ["frame-stall 1500"], 15.0),
            ("path-trace", ["frame-stall 0", 'path-trace "beauty"'], 10.0),
        ]
        rows = []
        for name, setup, duration in phases:
            for line in setup:
                must(line)
            must("frame 2")
            t0 = time.monotonic()
            while time.monotonic() - t0 < duration:
                t_send = time.monotonic() - t0
                text = must("session")
                t_recv = time.monotonic() - t0
                rows.append({
                    "phase": name, "t_send": round(t_send, 4), "t_recv": round(t_recv, 4),
                    "relay": number(text, "relay"), "applied": number(text, "applied"),
                    "pumped": number(text, "pumped"), "head": number(text, "head"),
                })
            if name == "path-trace":
                print(label, "path-trace-status:", must("path-trace-status"), flush=True)
        must('path-trace "off"')
        must("frame-stall 0")
        with open(os.path.join(outdir, label + ".csv"), "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)

        summarize(os.path.join(outdir, label + ".csv"))
        must("quit fast")
    finally:
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "--summarize":
        for p in sys.argv[2:]:
            summarize(p)
    elif len(sys.argv) == 4:
        main(*sys.argv[1:4])
    else:
        print(__doc__.strip())
        sys.exit(2)
