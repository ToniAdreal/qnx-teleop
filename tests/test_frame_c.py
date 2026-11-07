"""C codec tests: compile qnx/frame.c + test main with the host cc and run.

Golden vectors are literals produced by twin-teleop/src/telemetry.py
(the Python reference codec) on 2026-11-07, so this test cross-checks
the C implementation against the Python implementation byte for byte.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
QNX = ROOT / "qnx"


def test_c_codec_golden_vectors(tmp_path):
    binary = tmp_path / "test_frame"
    build = subprocess.run(
        ["cc", "-O2", "-Wall", "-Wextra", "-std=c99",
         "-I", str(QNX),
         str(QNX / "test_frame_main.c"), str(QNX / "frame.c"),
         "-o", str(binary)],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr
    assert "ALL PASS" in run.stdout
    for label in ("crc empty", "crc 123456789",
                  "vector A round-trip byte-identical",
                  "vector B round-trip byte-identical",
                  "vector C round-trip byte-identical",
                  "corrupted frame rejected (CRC)",
                  "bad magic rejected", "truncated frame rejected"):
        assert f"PASS {label}" in run.stdout, run.stdout
    assert "FAIL" not in run.stdout


def test_responder_compiles_clean(tmp_path):
    binary = tmp_path / "responder"
    build = subprocess.run(
        ["cc", "-O2", "-Wall", "-Wextra", "-std=c99",
         "-I", str(QNX),
         str(QNX / "teleop_responder.c"), str(QNX / "frame.c"),
         "-o", str(binary)],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    assert build.stderr == "", build.stderr
    assert binary.exists()
