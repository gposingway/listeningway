# ADR-0012: OpenRGB output consumer

## Status

Accepted — 2026-05-02

## Context

ADR-0010 established the `IOutputConsumer` abstraction and the vendoring
policy. ADR-0011 picked tinyosc for the OSC sender. This ADR records the
decisions for the OpenRGB sender: library pick, default frame mapping,
default rate, and the failure-mode story.

User wishlist research (the multi-community survey done earlier in v2.x
planning) showed audio-reactive RGB peripheral lighting as a recurring
ask, with the de-facto open path being: drive the user's existing
[OpenRGB](https://openrgb.org) server as a TCP client. We do not ship
RGB device drivers, do not enumerate vendor SDKs, do not bundle
OpenRGB itself. Listeningway just becomes another OpenRGB client.

## Decision

### A. Library: Youda008/OpenRGB-cppSDK, embedded

Vendor [`Youda008/OpenRGB-cppSDK`](https://github.com/Youda008/OpenRGB-cppSDK)
under `third_party/Youda008-OpenRGB-cppSDK/` as the wire-layer client.

- **License**: MIT (compatible with our existing third-party stack).
- **Footprint**: ~5500 LOC across the public headers (`include/OpenRGB/`),
  the implementation (`src/`), and two stdlib-style submodule helpers
  (`external/CppUtils-Essential/`, `external/CppUtils-Network/`).
- **Compiler support**: MSVC-tested; uses `_WIN32` paths in its socket
  layer (`Socket.cpp` calls `WSAStartup`).
- **API shape**: result-based (`ConnectStatus`, `RequestStatus`,
  `UpdateStatus`); no exceptions on the happy path. There are
  `*X()`-suffixed exception-throwing variants we deliberately don't use.

Hooked via `add_subdirectory(... EXCLUDE_FROM_ALL)` from our top-level
CMakeLists. The `orgbsdk` static target is the only thing we link;
upstream's `tools/orgbcli` CLI is excluded from our build by their own
EXCLUDE_FROM_ALL.

The cloned tree is checked in verbatim minus its `.git` metadata, with
`LICENSE` and `ATTRIBUTION.md` shipped alongside. `third_party/.gitignore`
exception explicitly tracks this directory.

### Library candidates considered

| Option | Outcome |
|---|---|
| **Youda008/OpenRGB-cppSDK** | **Picked.** Only serious C++ client; MIT; stdlib-only deps; handles protocol-version negotiation correctly (which the upstream documents as memory-corruption-prone if mishandled). |
| Hand-roll using cppSDK as reference | Rejected. ~600–900 LOC of careful but dull C++ for no upside. The user's "battle-tested code we can embed and properly credit" directive applies. |
| jath03/openrgb-python (port to C++) | Rejected. Python idioms don't carry. |
| openrgb-sdk (Node) / mt-inside/go-openrgb | Reference-only; both useful for cross-checking our wire-layer reading. |

### B. Wire flow

- Connect TCP to `host:port` (default `127.0.0.1:6742`).
- `requestDeviceList()` once on connect; cache the result.
- `switchToCustomMode(device)` for each device so subsequent direct LED
  writes take effect.
- Per worker tick: `setDeviceColors(device, vector<Color>)` with one
  color per LED.
- Every ~2 s of wall-clock: `checkForDeviceUpdates()`. If `OutOfDate`,
  refresh the device list. If the connection has dropped, reconnect
  and refresh.
- On addon unload: `disconnect()`.

### C. Default frame mapping (opinionated)

For each enumerated device, paint all LEDs as a spectrum-driven gradient:

- **Position → color**: linear position `t ∈ [0, 1]` along the device's
  LED list maps to a five-stop ramp (blue → cyan → green → yellow → red),
  bass-to-treble.
- **Position → amplitude**: `freqbands` is sampled at position `t` (with
  linear interpolation between adjacent bands). The raw band amplitude
  drives the color intensity.
- **Volume modulation**: AGC-normalized `volume_norm` adds a baseline
  intensity so even quiet content shows some color movement.
- **Beat flash**: `beat` (0..1, decaying after each detected onset) adds
  a pulse of brightness across all LEDs.
- **Brightness**: a global `0..1` multiplier from settings clamps the
  whole frame.

Color space and intensity formula (from
[`openrgb_consumer.cpp`](../../src/output/openrgb_consumer.cpp)):

```
intensity = clamp(band_val * 1.5 + vol * 0.3 + beat * 0.4, 0, 1) * brightness
out.rgb = ramp(t).rgb * intensity
```

This is the simplest mapping that visibly works on any device topology
(keyboards, mice, RAM strips, fan rings, case stripes) without
device-specific knowledge. It deliberately does not try to be smart
about zone roles — every LED participates as part of the spectrum.

Per-controller overrides (e.g. "use this device only for beat flash")
are a future refinement; this v1 mapping is intentionally simple.

### D. Defaults

| Setting | Default | Rationale |
|---|---|---|
| `enabled` | `false` | Toggleable consumers are off by default (ADR-0010). |
| `host` | `127.0.0.1` | OpenRGB server typically runs on the user's machine. |
| `port` | `6742` | OpenRGB SDK default. |
| `rate_hz` | `30` | OpenRGB server has a [documented CPU-wake-up issue](https://gitlab.com/CalcProgrammer1/OpenRGB/-/issues/2989) above ~60 Hz. 30 Hz is the friendly cadence. |
| `brightness` | `1.0` | No global dimming by default; users can dim via the overlay. |

`rate_hz` is clamped to `[5, 60]`. Below 5 Hz the visualization feels
sluggish; above 60 Hz we risk pinning the server's CPU.

### E. Threading and lifecycle

A single dedicated `std::thread` per `OpenRgbConsumer`. The worker owns
its `orgb::Client` (and thus the underlying TCP socket) strictly
thread-locally. Snapshot pulls go through `AudioSystem::snapshot()`
(wait-free seqlock).

State transitions inside `worker_main()`:

```
[connect] → [refresh devices] → loop:
                                  ├─ tick: setDeviceColors per device
                                  ├─ every ~2 s: checkForDeviceUpdates
                                  │    ├─ OutOfDate    → refresh
                                  │    ├─ Closed/Error → reconnect + refresh
                                  │    └─ UpToDate     → no-op
                                  └─ on send failure with ConnectionClosed:
                                        disconnect, reconnect on next tick
```

On `stop()`, the worker thread observes `running_=false`, exits the
loop, and `disconnect()`s before joining.

### F. Robustness commitments

- **Connection loss is recoverable**: detected via `RequestStatus::ConnectionClosed`
  or `UpdateStatus::ConnectionClosed`; status panel goes to "server
  connection lost; will retry" and the worker re-attempts on the next
  tick.
- **Hot-plug is supported**: handled by `checkForDeviceUpdates()`
  returning `OutOfDate`; we re-fetch the device list and switch each
  new device to custom mode.
- **No silent failures**: every error path writes to the consumer's
  `last_error_` field so the overlay surfaces it.
- **`switchToCustomMode` failures are not fatal**: some devices have a
  custom mode that's hard to reach; we record a warning but keep
  pushing frames to the rest.

### G. Test path: overlay "Send test packet"

The overlay's per-consumer test button (Phase 6) opens a short-lived
client, connects, and flashes all LEDs to white for one frame.
Confirms server reachability and per-device LED counts before flipping
the main toggle. Implemented via
`OpenRgbConsumer::send_test_packet()`.

## Consequences

### Positive

- **Out-of-the-box compatibility with the OpenRGB ecosystem.** Any
  device OpenRGB supports (hundreds of models across vendors)
  participates in the visualization with no per-vendor SDK work on our
  side.
- **No driver install on top of OpenRGB.** Users who already run
  OpenRGB get audio-reactive lighting by flipping one toggle.
- **Protocol-version negotiation is delegated to cppSDK.** We don't
  risk the documented memory-corruption failure modes of mishandling
  OpenRGB version differences.
- **Mapping is opinionated but visible.** "Every LED participates as
  part of the spectrum" guarantees the user sees motion as soon as
  audio plays — there's no zero-feedback failure mode where they
  enable the consumer and nothing changes.

### Negative

- **No per-device or per-zone customization in v1.** Users who want
  "case lighting on volume only" have to wait for a future ADR. v1
  paints everything as spectrum.
- **Mapping is opinionated.** Some users will hate the bass→treble
  color choice or the brightness math. Brightness is a knob; full
  remapping is not.
- **OpenRGB server must be running.** If it isn't, the consumer surfaces
  a connection error in the status line. We don't auto-launch
  OpenRGB; that's the user's responsibility.
- **vcpkg-style overlay port not available.** `add_subdirectory()`
  works; the cost is that the cppSDK source is part of our build
  every time, ~30 extra translation units. Build time impact is small
  but real.

### Neutral

- **No DMX / Art-Net / sACN bridge.** OSC (ADR-0011) covers
  professional lighting via DMX bridges already; OpenRGB covers
  consumer peripherals. The two ADRs together cover the primary user
  audiences.
- **No effect-mode driving (we always use custom/direct mode).** We
  could in principle drive the user's named effects (rainbow, breathe,
  etc.) instead of pushing per-LED frames, but custom mode is the
  one consistent path across all devices and the higher-bandwidth
  option. Worth revisiting if a specific use case demands it.

## Alternatives considered

### Hand-roll the OpenRGB protocol

**Rejected.** The wire format is documented but version-versioned
(protocol 0..5 currently), with the upstream Go author warning that
"sending the wrong version writes random stuff to memory, possibly
dangerous." cppSDK has the negotiation logic correct. ~600–900 LOC of
careful boilerplate to re-debug for no upside.

### Vendor only the protocol layer; write our own socket

**Rejected.** cppSDK's socket abstraction is part of `external/CppUtils-Network/`,
~1500 LOC, MSVC-aware, already integrated. Cherry-picking would mean
manual integration of the same code we'd be vendoring anyway, with
worse provenance.

### Use OpenRGB's plugin SDK instead of the network SDK

**Rejected.** Plugins live inside the OpenRGB process; we live inside
the game process. The network SDK is the right boundary.

### Drive named effects (Rainbow, Breathing, etc.) instead of custom-mode frames

**Deferred.** Custom mode gives us per-LED control and works on every
device that supports OpenRGB at all. Driving named effects is more
device-specific and harder to get visible feedback from. Reconsider
if users ask for it.

### Bundle OpenRGB itself

**Rejected.** OpenRGB is GPLv2 — incompatible with our vendoring policy
of permissive licenses, and the kernel-mode driver pieces are out of
scope for an in-process ReShade addon. OpenRGB stays a user-installed
prerequisite.

### Auto-detect "spectrum-friendly" zones (e.g. keyboard rows, RAM strips)

**Deferred.** Tempting, but no convention exists across vendors —
zone names are not standardized, zone types (`Single` / `Linear` /
`Matrix`) only weakly correlate with intent, and any heuristic will
be wrong somewhere. v1 paints all LEDs uniformly as part of the
spectrum, which is the lowest-surprise default.

## References

- ADR-0010 — `IOutputConsumer` abstraction; vendoring policy; security
  stance for toggleable network consumers.
- ADR-0011 — sibling ADR for the OSC consumer.
- [Youda008/OpenRGB-cppSDK on GitHub](https://github.com/Youda008/OpenRGB-cppSDK)
- [OpenRGB project home](https://openrgb.org)
- [`third_party/Youda008-OpenRGB-cppSDK/ATTRIBUTION.md`](../../third_party/Youda008-OpenRGB-cppSDK/ATTRIBUTION.md)
- [`src/output/openrgb_consumer.cpp`](../../src/output/openrgb_consumer.cpp)
- OpenRGB GitLab issue [#2989 (CPU wake-ups)](https://gitlab.com/CalcProgrammer1/OpenRGB/-/issues/2989)
- OpenRGB GitLab issue [#1273 (anti-cheat)](https://gitlab.com/CalcProgrammer1/OpenRGB/-/issues/1273) — affects the OpenRGB *server* due to its kernel-mode drivers; not a Listeningway exposure.
