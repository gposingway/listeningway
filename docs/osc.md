# OSC integration

Listeningway publishes its analysis as OSC messages over UDP. Anything that speaks OSC can subscribe: TouchDesigner, Resolume Arena and Avenue, Max/MSP, vvvv, MadMapper, SuperCollider, Pure Data, Sonic Pi, theatrical lighting controllers via DMX bridges, and so on.

The integration is **send-only**. Listeningway never opens a listening port; it just writes packets at a chosen cadence to a destination you specify.

## Quick check

In the overlay's **Integrations** section, click the **OSC** toggle. With the defaults (`127.0.0.1:9000`, 60 Hz), open a terminal and run the bundled receiver:

```
python samples/osc_receiver.py
```

It listens on `127.0.0.1:9000` and prints every incoming message. You should see `/listeningway/volume` and friends scrolling past as soon as audio plays.

If you want a single-shot reachability check before flipping the toggle, the overlay has a **Send test packet** button that fires `/listeningway/test 1.0` once over a short-lived socket.

## Settings

The settings live under `network.osc` in `Listeningway.json`, mirrored in the overlay's per-row Settings disclosure.

| Setting | Default | Range | Notes |
|---|---|---|---|
| `enabled` | `false` | bool | Off by default. Toggle from the overlay. |
| `host` | `127.0.0.1` | any IPv4 | Localhost by default; LAN exposure is opt-in. |
| `port` | `9000` | 1–65535 | TouchDesigner's default OSC In port. |
| `rate_hz` | `60` | 1–120 | Matches DSP cadence. Clamped at the worker. |

Live edits to `rate_hz` apply on the next worker tick. Edits to `host` or `port` require toggling the consumer off and on again so the socket rebinds.

## Address schema

OSC addresses mirror the shader uniform names under a `/listeningway/` prefix. The full registry below; semantic meanings match the entries in [STABILITY.md](../STABILITY.md).

| Address | Type tag | Shape | Range / units |
|---|---|---|---|
| `/listeningway/volume` | `f` | scalar | [0, 1] |
| `/listeningway/volumeleft` | `f` | scalar | [0, 1] |
| `/listeningway/volumeright` | `f` | scalar | [0, 1] |
| `/listeningway/volume_norm` | `f` | scalar | [0, ~4] AGC-normalized |
| `/listeningway/volume_att` | `f` | scalar | [0, ~4] smoothed |
| `/listeningway/bass_norm` | `f` | scalar | [0, ~4] |
| `/listeningway/mid_norm` | `f` | scalar | [0, ~4] |
| `/listeningway/treb_norm` | `f` | scalar | [0, ~4] |
| `/listeningway/bass_att` | `f` | scalar | [0, ~4] |
| `/listeningway/mid_att` | `f` | scalar | [0, ~4] |
| `/listeningway/treb_att` | `f` | scalar | [0, ~4] |
| `/listeningway/audiopan` | `f` | scalar | [-1, +1] |
| `/listeningway/audioformat` | `f` | scalar | channel count {0, 1, 2, 6, 8} |
| `/listeningway/numbands` | `f` | scalar | live band count |
| `/listeningway/freqbands` | `fff…` | array | length = `numbands`, each [0, 1] |
| `/listeningway/direction8` | `ffffffff` | array of 8 | each [0, 1]; F, FR, R, BR, B, BL, L, FL |
| `/listeningway/beat` | `f` | scalar | [0, 1], 1.0 on onset, decays |
| `/listeningway/beat_phase` | `f` | scalar | [0, 1) PLL-locked when tempo is locked |
| `/listeningway/tempo_bpm` | `f` | scalar | BPM, 0 if undetected |
| `/listeningway/tempo_confidence` | `f` | scalar | [0, 1] |
| `/listeningway/phase_volume` | `f` | scalar | [0, 1) chronotensity |
| `/listeningway/phase_bass` | `f` | scalar | [0, 1) |
| `/listeningway/phase_treble` | `f` | scalar | [0, 1) |
| `/listeningway/spectral_centroid` | `f` | scalar | [0, 1] brightness |
| `/listeningway/loudness` | `f` | scalar | [0, 1] K-weighted (BS.1770) momentary, linear |
| `/listeningway/test` | `f` (1.0) | scalar | overlay test button only |

22 messages per tick at the typical settings. At 60 Hz that's roughly 1.3 K packets/sec, each ≤ 300 bytes. Trivial bandwidth on localhost.

## Wire details

- Transport is **UDP**. OSC is conventionally UDP, receivers expect that, and dropping a stale packet is preferable to blocking the worker thread on redelivery.
- One **OSC message per UDP packet**. No bundles, no time tags. Bundles would compress per-tick traffic but add encoder allocations and most receivers handle individual messages fine.
- Arrays are encoded as repeated `f` type tags (e.g. `,fffff…f`) rather than the optional OSC `[ ]` bracket syntax. TouchDesigner, Resolume, and stdlib-style Python OSC parsers accept both equivalently.
- Send-only. Listeningway never binds a listening socket. There is no incoming-message path in v2.x.

## Integration recipes

### TouchDesigner

1. Drop an **OSC In** CHOP into your network. Default network port `9000` already matches Listeningway's default.
2. The CHOP exposes one channel per address path. `/listeningway/freqbands` becomes 64 channels named `freqbands1`...`freqbands64` (TouchDesigner array unpacking).
3. Wire the channels you want into the rest of your network: a Volume CHOP into a Transform TOP for a kick-driven scale, `freqbands` into a CHOP To DAT for a per-band texture lookup, etc.

### Resolume Arena / Avenue

1. **Preferences → OSC → OSC Input**, port `9000`. Enable.
2. **Edit → Shortcuts → OSC**. Right-click any parameter in the deck (e.g. layer opacity), pick **Shortcuts → Edit OSC**, then click the parameter and play audio. Resolume captures the address and you can pin it.
3. For arrays like `freqbands`, use Resolume's **Multiple Values** option on the OSC shortcut.

### Max/MSP

```
[udpreceive 9000] → [unpack 0. 0. 0. ...] → your patch
                  ↘ [route /listeningway/volume /listeningway/beat ...]
```

The `[udpreceive]` object handles OSC parsing if you turn on its `unpack` attribute, or use `[oscparse]` from the CNMAT externals.

### vvvv / Pure Data / SuperCollider / others

Any tool with a stdlib-quality OSC parser will work. Point it at `127.0.0.1:9000`, parse `/listeningway/*`. If your tool needs a fixed-size band array, subscribe to `/listeningway/freqbands` and read `/listeningway/numbands` to size it on the fly; the pre-binned 16/32-band reductions exposed to shaders aren't in the OSC tree in v2.x, but you can average down in your patch.

## Limitations

- **No live `host` / `port` rebinding.** Changing host or port requires toggling the consumer off and on.
- **Send-only.** OSC is a bidirectional protocol; an incoming control-surface path could land later but isn't there in v2.x.
- **No bundles.** Slight per-tick UDP overhead (22 small headers per tick). Negligible at localhost; relevant only if you're routing the stream over a constrained WAN, which is out of scope for v2.x.

## See also

- [ADR-0011](adr/0011-osc-output.md): full design rationale, library choice, alternatives considered.
- [ADR-0010](adr/0010-network-outputs.md): the `IOutputConsumer` abstraction and the security stance for toggleable network outputs.
- [STABILITY.md](../STABILITY.md): the shader uniform contract; OSC addresses mirror it 1:1.
- [`samples/osc_receiver.py`](../samples/osc_receiver.py): the bundled debug receiver.
- [`third_party/tinyosc/ATTRIBUTION.md`](../third_party/tinyosc/ATTRIBUTION.md): credit and update procedure for the vendored OSC encoder.
