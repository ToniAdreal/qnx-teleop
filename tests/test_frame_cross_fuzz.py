"""Cross-implementation fuzz: Python reference codec vs C codec.

Generates deterministic random frames with the twin-teleop Python
reference codec (seed fixed), feeds them to the C fuzz driver, and
requires byte-identical decode + re-encode. Also flips single bytes
and requires the C decoder to reject the corruption (CRC, magic, or
length errors as applicable).

Seeded (seed 20261108) so the run is reproducible, not random per run.
"""
from __future__ import annotations

import math
import random
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QNX = ROOT / "qnx"
TWIN_TELEOP_SRC = (
    ROOT.parent / "twin-teleop" / "src"
)

sys.path.insert(0, str(TWIN_TELEOP_SRC))

from telemetry import TelemetryFrame, encode  # noqa: E402

SEED = 20261108
N_FRAMES = 128
N_CORRUPT = 128


def _random_frame(rng: random.Random) -> TelemetryFrame:
    n = rng.randint(1, 16)
    ts = rng.choice([0, 1, 1_000_000, 2**63 - 1, 2**64 - 1,
                     rng.randrange(0, 2**64)])
    edge = [0.0, -0.0, 1.0, -1.0, math.pi, -math.pi, 1e-30, 5.0, -5.0]
    positions = tuple(
        rng.choice(edge) if rng.random() < 0.3
        else rng.uniform(-math.pi, math.pi)
        for _ in range(n)
    )
    torques = tuple(
        rng.choice(edge) if rng.random() < 0.3
        else rng.uniform(-5.0, 5.0)
        for _ in range(n)
    )
    return TelemetryFrame(timestamp_us=ts, positions=positions,
                          torques=torques)


def _build_driver(tmp_path: Path) -> Path:
    binary = tmp_path / "fuzz_driver"
    build = subprocess.run(
        ["cc", "-O2", "-Wall", "-Wextra", "-std=c99",
         "-I", str(QNX),
         str(QNX / "fuzz_driver.c"), str(QNX / "frame.c"),
         "-o", str(binary)],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    assert build.stderr == "", build.stderr
    return binary


def _run_driver(binary: Path, frames_hex: list[str]) -> list[str]:
    run = subprocess.run(
        [str(binary)], input="\n".join(frames_hex) + "\n",
        capture_output=True, text=True, timeout=60)
    assert run.returncode == 0, run.stdout + run.stderr
    lines = run.stdout.splitlines()
    assert len(lines) == len(frames_hex), (
        f"driver returned {len(lines)} lines for {len(frames_hex)} frames: "
        f"{run.stdout[:500]}")
    return lines


def test_cross_fuzz_round_trip(tmp_path):
    rng = random.Random(SEED)
    frames = [_random_frame(rng) for _ in range(N_FRAMES)]
    wire = [encode(f) for f in frames]
    binary = _build_driver(tmp_path)
    lines = _run_driver(binary, [w.hex() for w in wire])
    for i, (line, raw) in enumerate(zip(lines, wire)):
        assert line == "OK " + raw.hex(), (
            f"frame {i} (n={len(frames[i].positions)}): "
            f"driver said {line!r}, expected OK echo of {raw.hex()}")


def test_cross_fuzz_corruption_rejected(tmp_path):
    rng = random.Random(SEED + 1)
    frames = [_random_frame(rng) for _ in range(N_CORRUPT)]
    wire = [bytearray(encode(f)) for f in frames]
    corrupted = []
    expected = []
    for raw in wire:
        raw = bytearray(raw)
        # Flip one byte in the timestamp, payload, or CRC field (never
        # magic/version/count: those give -1/-2, covered by the other
        # tests). Any such flip must break the CRC check -> -3.
        pos = rng.randrange(4, len(raw))
        raw[pos] ^= 0x01
        corrupted.append(bytes(raw))
        expected.append(-3)
    binary = _build_driver(tmp_path)
    lines = _run_driver(binary, [c.hex() for c in corrupted])
    for i, (line, want) in enumerate(zip(lines, expected)):
        assert line == f"ERR {want}", (
            f"corrupted frame {i}: driver said {line!r}, "
            f"expected ERR {want}")


def test_cross_fuzz_bad_magic_rejected(tmp_path):
    rng = random.Random(SEED + 2)
    frames = [_random_frame(rng) for _ in range(16)]
    wire = [bytearray(encode(f)) for f in frames]
    for raw in wire:
        raw[0] = ord("X")
    binary = _build_driver(tmp_path)
    lines = _run_driver(binary, [bytes(w).hex() for w in wire])
    for i, line in enumerate(lines):
        assert line == "ERR -1", (
            f"bad-magic frame {i}: driver said {line!r}, expected ERR -1")
