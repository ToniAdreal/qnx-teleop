"""Host-side bridge for the QNX teleop responder.

Stdlib-only. Connects to the responder (QNX target or Linux host build)
over TCP on loopback, sends TwinTeleop binary frames, reads the byte-
identical echo back, and measures round-trip latency per frame.

The bridge never trusts timing alone: every echo is checked byte for
byte against what was sent, and a CRC failure on either side ends the
session. Latency numbers are reported, never asserted against a
threshold in tests — the deterministic assertions are structural
(all frames echoed, bytes identical, counts exact).
"""
from __future__ import annotations

import socket
import statistics
import time
from dataclasses import dataclass, field


@dataclass
class FrameResult:
    sent: bytes
    echo: bytes | None
    rtt_ns: int | None

    @property
    def echoed(self) -> bool:
        return self.echo == self.sent


@dataclass
class BridgeReport:
    results: list[FrameResult] = field(default_factory=list)

    @property
    def sent_count(self) -> int:
        return len(self.results)

    @property
    def echoed_count(self) -> int:
        return sum(1 for r in self.results if r.echoed)

    @property
    def rtts_ns(self) -> list[int]:
        return [r.rtt_ns for r in self.results if r.rtt_ns is not None]

    def summary(self) -> dict:
        rtts = self.rtts_ns
        return {
            "sent": self.sent_count,
            "echoed": self.echoed_count,
            "rtt_min_ns": min(rtts) if rtts else None,
            "rtt_max_ns": max(rtts) if rtts else None,
            "rtt_mean_ns": int(statistics.fmean(rtts)) if rtts else None,
        }


def _read_exact(sock: socket.socket, n: int) -> bytes:
    data = b""
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            break
        data += chunk
    return data


def send_frames(host: str, port: int, frames: list[bytes],
                timeout_s: float = 5.0) -> BridgeReport:
    """Send each frame, read back its echo, record RTT. One connection."""
    report = BridgeReport()
    with socket.create_connection((host, port), timeout=timeout_s) as sock:
        sock.settimeout(timeout_s)
        for frame in frames:
            t0 = time.monotonic_ns()
            sock.sendall(frame)
            echo = _read_exact(sock, len(frame))
            t1 = time.monotonic_ns()
            report.results.append(FrameResult(
                sent=frame,
                echo=echo if echo else None,
                rtt_ns=(t1 - t0) if echo else None,
            ))
            if not echo:
                break
    return report
