# qnx-teleop — Real-Time Telemetry Responder on QNX SDP 8.0

Entry for the **QNX Global Hackathon** (Robotics & Medical track,
deadline 2026-12-15 14:00 ET). A real-time telemetry responder that runs
on QNX SDP 8.0 inside QEMU (or on a Raspberry Pi 5 target) and speaks
the TwinTeleop binary telemetry protocol: it receives multi-axis
manipulator joint frames, validates them, and echoes them back as an
acknowledgement, while the host bridge measures round-trip latency.

The point is the real-time property, not the echo: on QNX Neutrino the
serving thread runs at `SCHED_FIFO` priority 21 and timestamps service
time with `ClockCycles()`, so the responder demonstrates deterministic,
bounded-latency handling of teleoperation traffic — the same traffic
class TwinTeleop streams from a real arm at CyberOrigin with a <30 ms
end-to-end budget.

## Status (honest)

- [x] **M1 (2026-11-07):** C codec for the TwinTeleop frame format,
      cross-checked byte-for-byte against the Python reference codec
      (golden vectors); host-side bridge + loopback end-to-end test
      against the responder's Linux host build. All green, verified in
      an isolated copy.
- [ ] **M2:** myQNX account + SDP 8.0 via QNX Everywhere, QEMU target
      boot. **Blocked on a user step:** myQNX registration is
      email + password only (no Google SSO), so the user sets the
      password once via Secure Vault; then the SDP download proceeds.
- [ ] **M3:** cross-compile with `qcc`, boot QNX 8.0 in QEMU, run the
      responder on the target, measure real service-time distribution.
- [ ] **M4:** Devpost submission (public repo + <3 min demo video).

## Layout

- `qnx/frame.h`, `qnx/frame.c` — the frame codec (ISO C99, no POSIX).
- `qnx/teleop_responder.c` — TCP responder; QNX real-time calls are
  guarded by `#ifdef __QNX__`, Linux host build uses
  `clock_gettime(CLOCK_MONOTONIC)` for the same accounting.
- `qnx/test_frame_main.c` — codec test main with golden vectors.
- `src/host_bridge.py` — stdlib-only Python bridge: sends frames, checks
  byte-identical echoes, reports RTT stats.
- `tests/` — pytest suite (C codec + loopback end-to-end).

## Run the tests

```bash
make host-test        # pytest tests/ -q
```

## Run the responder (host build, for testing only)

```bash
make responder
./responder 5555
```

## Wire format

Little-endian `TT` frames, identical to
[twin-teleop](https://github.com/ToniAdreal/twin-teleop): 2-byte magic,
version 1, joint count 1–16, uint64 microsecond timestamp, N float32
positions (rad), N float32 torques (N·m), CRC-16/CCITT-FALSE.
