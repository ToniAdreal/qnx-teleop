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
- [x] **M2a (2026-11-08):** host-side codec hardening while M2 waits on
      the myQNX step — deterministic cross-implementation fuzz
      (seed 20261108): 128 random frames (1–16 joints, edge values
      incl. 0 / -0.0 / ±π / 2^64-1 timestamps) encoded by the Python
      reference codec, decoded + re-encoded byte-identically by the C
      codec; 128 single-byte corruptions all rejected with CRC error;
      16 bad-magic frames all rejected. All green, verified in an
      isolated copy.
- [x] **M2b (2026-11-09):** responder malformed-input resilience —
      bad magic / bad version / joint-count 0 / joint-count 17 /
      truncated frame / 10-client connect-and-close storm are each
      dropped without echo, the listener stays alive through all of
      them, and a fresh valid client still gets all 3 golden frames
      echoed byte-identically. Suite now 8/8, verified in an isolated
      clone. (Host build is the test vehicle; the same serve_client()
      code ships to the QNX target.)
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
- `qnx/fuzz_driver.c` — stdin/stdout driver for cross-implementation
  fuzz: decodes hex frames, re-encodes, reports OK / MISMATCH / ERR.
- `tests/test_frame_cross_fuzz.py` — seeded cross-fuzz (Python reference
  codec vs C codec): 128 round-trips, 128 corruptions, 16 bad-magic.
- `tests/test_responder_robustness.py` — malformed-input resilience:
  abusive clients are dropped (FIN or RST, no echo) and the listener
  keeps serving valid clients afterwards.
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
