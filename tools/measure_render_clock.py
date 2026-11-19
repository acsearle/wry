#!/usr/bin/env python3
"""Measure the render clock (docs/step_pump.md section 7) on a live client.

    measure_render_clock.py BINARY OUTDIR [LABEL]

Launches BINARY --offscreen --no-tests --skip-splash with its own control
socket and settings dir, loads pt_reference, and samples `session` back
to back: offscreen frames run on demand, one per command, so each
sample is a frame.  From each it takes the newest adopted World's time
(`time`), the position the frame drew (`render`), the clock's delay, its
underrun and snap counts, and the relay's head.  Phases: fast frames,
slow frames (`frame-stall`), paused, resumed.

For each phase it prints:
  frames      samples taken
  frame ms    mean spacing of the replies
  render/s    the drawn position's rate, steps per wall second
  rms newest  RMS residual of the newest World's time from a straight
              line in wall time, per 1 s chunk (steps): what drawing the
              newest World would show
  rms render  the same for the drawn position: the clock's smoothness
  behind      mean newest - render (steps)
  delay       the clock's delay at the end of the phase (steps)
  underruns, snaps   counted during the phase
Writes OUTDIR/LABEL.csv.
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


def number(text, key, kind=int):
    m = re.search(r"\b" + key + r" ([0-9.]+)", text)
    return kind(m.group(1)) if m else None


def rms_residual(ts, ys, chunk=1.0):
    """RMS of y about its least-squares line in t, fitted per chunk of t."""
    total, count = 0.0, 0
    i = 0
    while i < len(ts):
        j = i
        while j < len(ts) and ts[j] - ts[i] < chunk:
            j += 1
        if j - i >= 3:
            t, y = ts[i:j], ys[i:j]
            n = len(t)
            mt, my = sum(t) / n, sum(y) / n
            stt = sum((a - mt) ** 2 for a in t)
            b = sum((a - mt) * (c - my) for a, c in zip(t, y)) / stt if stt else 0.0
            for a, c in zip(t, y):
                r = c - (my + b * (a - mt))
                total += r * r
                count += 1
        i = j
    return (total / count) ** 0.5 if count else float("nan")


def main(binary, outdir, label="render_clock"):
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
        print(label, "loaded:", must("session"), flush=True)

        phases = [
            ("fast", ["frame-stall 0"], 8.0),
            ("stall-30ms", ["frame-stall 30"], 6.0),
            ("stall-250ms", ["frame-stall 250"], 6.0),
            ("fast-again", ["frame-stall 0"], 4.0),
            ("paused", ["pause"], 3.0),
            ("resumed", ["resume"], 5.0),
        ]
        rows = []
        for name, setup, duration in phases:
            for line in setup:
                must(line)
            t0 = time.monotonic()
            while time.monotonic() - t0 < duration:
                text = must("session")
                t = time.monotonic() - t0
                rows.append({
                    "phase": name, "t": round(t, 5),
                    "time": number(text, "time"), "render": number(text, "render", float),
                    "delay": number(text, "delay", float),
                    "underruns": number(text, "underruns"), "snaps": number(text, "snaps"),
                    "relay": number(text, "relay"), "pumped": number(text, "pumped"),
                })
        must("frame-stall 0")
        with open(os.path.join(outdir, label + ".csv"), "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)

        print(f"{'phase':12s} {'frames':>6s} {'frame ms':>8s} {'render/s':>8s} {'rms newest':>10s}"
              f" {'rms render':>10s} {'behind':>6s} {'delay':>5s} {'underruns':>9s} {'snaps':>5s}")
        for name, _, _ in phases:
            ph = [r for r in rows if r["phase"] == name]
            if len(ph) < 3:
                print(name, "too few samples", len(ph))
                continue
            # the first second of a phase is its transient
            steady = [r for r in ph if r["t"] >= 1.0] or ph
            ts = [r["t"] for r in steady]
            dt = ts[-1] - ts[0]
            spacing = 1000.0 * (ph[-1]["t"] - ph[0]["t"]) / (len(ph) - 1)
            rate = (steady[-1]["render"] - steady[0]["render"]) / dt if dt > 0 else float("nan")
            rms_newest = rms_residual(ts, [float(r["time"]) for r in steady])
            rms_render = rms_residual(ts, [r["render"] for r in steady])
            behind = sum(r["time"] - r["render"] for r in steady) / len(steady)
            print(f"{name:12s} {len(ph):6d} {spacing:8.1f} {rate:8.1f} {rms_newest:10.3f}"
                  f" {rms_render:10.3f} {behind:6.2f} {ph[-1]['delay']:5.2f}"
                  f" {ph[-1]['underruns'] - ph[0]['underruns']:9d} {ph[-1]['snaps'] - ph[0]['snaps']:5d}",
                  flush=True)
        must("quit fast")
    finally:
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.kill()
        log.close()


if __name__ == "__main__":
    main(*sys.argv[1:4])
