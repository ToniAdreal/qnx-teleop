"""End-to-end loopback test: Python bridge <-> C responder (host build).

Starts the compiled responder on an ephemeral loopback port, sends the
three golden frames, and asserts every echo is byte-identical to what
was sent. A fourth check sends a corrupted frame and asserts the
responder drops the connection instead of echoing garbage.

No timing thresholds are asserted: RTT numbers are reported in the
summary only. The deterministic assertions are structural.
"""
from __future__ import annotations

import socket
import subprocess
import time
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))

from host_bridge import send_frames  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
QNX = ROOT / "qnx"

GOLDEN_HEX = [
    # 6 joints, ts 1000000
    "5454010640420f0000000000cdcccc3dcdcc4cbe9a99993e00000000f90fc93ff90"
    "fc9bf0000003f000000bf000000000000a03f000000c00000403f0fb8",
    # 2 joints, ts 1001000
    "5454010228460f000000000000000000000000000000000000000000c736",
    # 16 joints, ts 2000000
    "5454011080841e0000000000000000000ad7233c0ad7a33c8fc2f53c0ad7233dcdcc"
    "4c3d8fc2753d295c8f3d0ad7a33dec51b83dcdcccc3dae47e13d8fc2f53db81e053e29"
    "5c0f3e9a99193e000000800ad7a3bc0ad723bd8fc275bd0ad7a3bdcdccccbd8fc2f5bd"
    "295c0fbe0ad723beec5138becdcc4cbeae4761be8fc275beb81e85be295c8fbe9a9999"
    "be7b36",
]

GOLDEN = [bytes.fromhex(h) for h in GOLDEN_HEX]


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def _build_responder(tmp_path: Path) -> Path:
    binary = tmp_path / "responder"
    build = subprocess.run(
        ["cc", "-O2", "-Wall", "-Wextra", "-std=c99",
         "-I", str(QNX),
         str(QNX / "teleop_responder.c"), str(QNX / "frame.c"),
         "-o", str(binary)],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    return binary


def _wait_listening(port: int, proc: subprocess.Popen, timeout_s=5.0):
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise AssertionError(f"responder exited early rc={proc.returncode}")
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.5):
                return
        except OSError:
            time.sleep(0.05)
    raise AssertionError("responder did not start listening")


def test_loopback_round_trip(tmp_path):
    binary = _build_responder(tmp_path)
    port = _free_port()
    proc = subprocess.Popen(
        [str(binary), str(port)],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    try:
        _wait_listening(port, proc)
        report = send_frames("127.0.0.1", port, GOLDEN)
        assert report.sent_count == 3
        assert report.echoed_count == 3, report.summary()
        for result in report.results:
            assert result.echo == result.sent
            assert result.rtt_ns is not None and result.rtt_ns > 0
    finally:
        proc.terminate()
        _, stderr = proc.communicate(timeout=5)
    assert "served=3 rejected=0" in stderr


def test_corrupted_frame_drops_connection(tmp_path):
    binary = _build_responder(tmp_path)
    port = _free_port()
    proc = subprocess.Popen(
        [str(binary), str(port)],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    try:
        _wait_listening(port, proc)
        bad = bytearray(GOLDEN[0])
        bad[20] ^= 0x01  # break the CRC
        with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
            sock.settimeout(5)
            sock.sendall(bytes(bad))
            echo = sock.recv(len(bad))
            # Responder must not echo a corrupt frame; connection closes.
            assert echo == b"", echo
    finally:
        proc.terminate()
        _, stderr = proc.communicate(timeout=5)
    assert "rejected=1" in stderr
