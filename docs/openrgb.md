# OpenRGB integration

Listeningway can drive RGB peripherals through your existing [OpenRGB](https://openrgb.org) server. It doesn't ship RGB drivers, doesn't enumerate vendor SDKs, and doesn't bundle OpenRGB itself: Listeningway is just another OpenRGB SDK client. Anything OpenRGB sees, Listeningway can paint.

## Prerequisites

1. Install [OpenRGB](https://openrgb.org) and confirm it can see your hardware. (Keyboards, mice, RAM strips, fan rings, case stripes, AIO pumps, GPU lights, motherboard zones, ARGB headers, ...)
2. In OpenRGB, open **Settings → SDK Server → Enable Server**. Default port is `6742`. Leave the host on `127.0.0.1` unless you're driving lights from another machine.
3. Keep OpenRGB running while you play. Listeningway is a client; it reconnects automatically if the server goes away, but it can't auto-launch the server for you.

## Quick check

In the overlay's **Integrations** section, click the **OpenRGB** toggle. With the defaults (`127.0.0.1:6742`, 30 Hz), Listeningway connects, discovers your devices, and starts painting all LEDs as a spectrum-driven gradient.

If you'd rather verify reachability before flipping the main toggle, the overlay has a **Send test packet** button per integration. For OpenRGB it opens a short-lived client, connects, and flashes every LED to white for one frame.

If the toggle flips itself back off and the status line says "server connection lost", OpenRGB isn't reachable. Check that the SDK server is enabled and on the expected port.

## Settings

The settings live under `network.openrgb` in `Listeningway.json`, mirrored in the overlay's per-row Settings disclosure.

| Setting | Default | Range | Notes |
|---|---|---|---|
| `enabled` | `false` | bool | Off by default. Toggle from the overlay. |
| `host` | `127.0.0.1` | any IPv4 | Localhost by default. |
| `port` | `6742` | 1–65535 | OpenRGB SDK default. |
| `rate_hz` | `30` | 5–60 | OpenRGB has [a documented CPU-wake-up issue](https://gitlab.com/CalcProgrammer1/OpenRGB/-/issues/2989) above ~60 Hz, so 30 Hz is the friendly cadence. |
| `brightness` | `1.0` | 0.0–1.0 | Global multiplier on the whole frame. Useful when devices are too bright at night. |

`rate_hz` is clamped to `[5, 60]`.

## What gets driven

The default mapping is opinionated and the same on every device. For each enumerated device, all LEDs participate in a spectrum-driven gradient:

- **Position → color**: linear position `t ∈ [0, 1]` along the device's LED list maps to a five-stop ramp (blue → cyan → green → yellow → red), bass-to-treble.
- **Position → amplitude**: `freqbands` is sampled at position `t` (with linear interpolation between adjacent bands). The raw band amplitude drives the color intensity.
- **Volume modulation**: AGC-normalized `volume_norm` adds a baseline intensity so even quiet content shows some color movement.
- **Beat flash**: `beat` (0..1, decaying after each detected onset) adds a brightness pulse across all LEDs.
- **Brightness**: the global `brightness` setting multiplies the whole frame.

The intensity formula:

```
intensity = clamp(band_val * 1.5 + vol * 0.3 + beat * 0.4, 0, 1) * brightness
out.rgb = ramp(t).rgb * intensity
```

This is the simplest mapping that visibly works on any device topology without device-specific knowledge, and it guarantees you see motion as soon as audio plays. There's no zero-feedback failure mode where the toggle is on and nothing changes.

Per-device or per-zone customization (e.g. "case stripes on volume only", "keyboard on beat flash only") isn't in v1; it's on the v2.x roadmap. If you want a specific device left alone, disable that controller in OpenRGB itself.

## Failure modes

The integration is designed to recover automatically rather than need manual restart.

- **Server not running on toggle-on.** The connection fails and the overlay toggle flips back off. Start OpenRGB and toggle on again.
- **Server crashes or restarts mid-session.** The status line shows "server connection lost; will retry" and Listeningway reconnects automatically once the server is back. Devices are re-discovered on reconnect.
- **Hot-plug.** New devices added or removed while Listeningway is running are picked up within a couple of seconds; no manual refresh needed.
- **Custom-mode unavailable on one device.** Some devices expose a custom mode that's hard to reach from OpenRGB. Listeningway keeps painting the rest; the troublesome device is left in whatever mode it was already in.
- **A device shows up but doesn't physically respond.** You may notice that a peripheral appears in OpenRGB and gets reported correctly in Listeningway's status line, yet the LEDs don't react to anything — not to OpenRGB's own colour wheel, not to Listeningway, not to anything. This is sometimes a firmware-stuck state where the device has locked itself into an onboard profile that ignores host commands; it isn't specific to Listeningway and isn't an OpenRGB bug. One possible solution that's known to help on some Roccat keyboards (e.g. the Vulcan Pro): install the vendor's software (Roccat Swarm, in that case), use its **Reset to Factory Defaults** option once, then close it. After that the device usually responds to OpenRGB and Listeningway normally; the vendor software can be uninstalled afterward if you don't want it sitting around.

## Limitations

- **One opinionated mapping.** No per-device or per-zone configuration in v1. Every LED on every controller participates in the spectrum.
- **Custom-mode only.** Frames are pushed per-LED via custom/direct mode rather than driving named effects (Rainbow, Breathing, etc.). Custom mode is the one consistent path across all OpenRGB-supported devices.
- **No effect-mode driving.** If your device has a particularly nice built-in effect you want to keep using, the workaround is to disable that controller in OpenRGB so Listeningway doesn't see it.
- **OpenRGB itself must be running.** Listeningway won't auto-launch it.
- **The OpenRGB server's [anti-cheat caveat](https://gitlab.com/CalcProgrammer1/OpenRGB/-/issues/1273)** affects the server (kernel-mode drivers), not Listeningway. Listeningway is a normal user-mode TCP client.

## See also

- [ADR-0012](adr/0012-openrgb-output.md): full design rationale, library choice, mapping math, alternatives considered.
- [ADR-0010](adr/0010-network-outputs.md): the `IOutputConsumer` abstraction and the security stance for toggleable network outputs.
- [Youda008/OpenRGB-cppSDK](https://github.com/Youda008/OpenRGB-cppSDK): the vendored wire-layer client.
- [`third_party/Youda008-OpenRGB-cppSDK/ATTRIBUTION.md`](../third_party/Youda008-OpenRGB-cppSDK/ATTRIBUTION.md): credit and update procedure.
- [OpenRGB project home](https://openrgb.org).
