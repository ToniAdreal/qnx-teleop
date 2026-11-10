"""Responder robustness: malformed input must never kill the listener.

Abusive clients (bad magic, bad version, out-of-range joint count,
truncated frame, connect-and-close storm) are each dropped by the
responder, and the process must keep accepting afterwards: a fresh
valid client still gets every golden frame echoed byte-identically.

This is the mission-critical property the QNX track cares about: a
telemetry responder that dies on the first corrupt packet is not
real-time infrastructure. The host build is the test vehicle; the
same serve_client() code ships to the QNX target.
"""
from __future__ import annotations

import socket
import subprocess
import time
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))

from host_bridge import send_frames  # noqa: E402
from test_host_bridge import GOLDEN, _build_responder, _free_port, _wait_listening  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent


def _abuse_and_expect_drop(port: int, payload: bytes) -> None:
    """Send payload, expect the responder to close without echoing.

    A clean FIN (recv returns b"") is the tidy case; a RST
    (ConnectionResetError) is also a drop: the responder closes while
    unread frame bytes are still in its receive buffer, and the kernel
    resets the connection. Either way no echo bytes arrive, which is
    the property under test.
    """
    with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
        sock.settimeout(5)
        sock.sendall(payload)
        try:
            echo = sock.recv(4096)
        except ConnectionResetError:
            echo = b""
        assert echo == b"", echo


def _assert_alive(proc: subprocess.Popen) -> None:
    assert proc.poll() is None, f"responder died rc={proc.returncode}"


def test_responder_survives_malformed_clients(tmp_path):
    binary = _build_responder(tmp_path)
    port = _free_port()
    proc = subprocess.Popen(
        [str(binary), str(port)],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    try:
        _wait_listening(port, proc)

        # Baseline: valid client served.
        report = send_frames("127.0.0.1", port, GOLDEN)
        assert report.echoed_count == 3, report.summary()
        _assert_alive(proc)

        # 1. Bad magic: 'XX' instead of 'TT'.
        bad_magic = bytearray(GOLDEN[0])
        bad_magic[0:2] = b"XX"
        _abuse_and_expect_drop(port, bytes(bad_magic))
        _assert_alive(proc)

        # 2. Bad version: version byte 2 instead of 1.
        bad_version = bytearray(GOLDEN[0])
        bad_version[2] = 2
        _abuse_and_expect_drop(port, bytes(bad_version))
        _assert_alive(proc)

        # 3. Joint count 0 (below range).
        bad_count_low = bytearray(GOLDEN[1])
        bad_count_low[3] = 0
        _abuse_and_expect_drop(port, bytes(bad_count_low))
        _assert_alive(proc)

        # 4. Joint count 17 (above range).
        bad_count_high = bytearray(GOLDEN[1])
        bad_count_high[3] = 17
        _abuse_and_expect_drop(port, bytes(bad_count_high))
        _assert_alive(proc)

        # 5. Truncated frame: 5 header bytes, then close.
        with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
            sock.sendall(GOLDEN[0][:5])
        time.sleep(0.2)
        _assert_alive(proc)

        # 6. Connect-and-close storm: 10 clients, no bytes at all.
        for _ in range(10):
            with socket.create_connection(("127.0.0.1", port), timeout=5):
                pass
        _assert_alive(proc)

        # Recovery: a fresh valid client is still served in full.
        report = send_frames("127.0.0.1", port, GOLDEN)
        assert report.sent_count == 3
        assert report.echoed_count == 3, report.summary()
        for result in report.results:
            assert result.echo == result.sent
        _assert_alive(proc)
    finally:
        proc.terminate()
        _, stderr = proc.communicate(timeout=5)
    # 1 baseline client + 4 rejected + 1 truncated + 10 storm + 1 recovery
    # = 17 "client done" lines; 4 rejected in total.
    assert stderr.count("client done:") >= 16, stderr
    assert stderr.count("rejected=1") == 4, stderr
    assert "served=3 rejected=0" in stderr
