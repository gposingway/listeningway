# ADR-0011: OSC output consumer

## Status

Accepted — 2026-05-02

## Context

ADR-0010 established `IOutputConsumer` and the vendoring policy for
toggleable network outputs. This ADR records the specific decisions for
the OSC sender: library pick, address schema, default rate, and the
delivery mechanism.

User wishlist research (`research-notes-process-audio.md` companion findings)
showed that OSC is the lingua franca of the creative-coding / VJ ecosystem
— TouchDesigner, Resolume, vvvv, Max/MSP, MadMapper, theatrical lighting
controllers via DMX bridges. Third-party AudioLink↔OSC bridges exist
(Codel1417/VRC-OSC-Audio-Reaction, OSCAudioLink, Mathieu52/OSCMidi)
precisely because AudioLink itself doesn't speak OSC. The demand is
concrete and cross-community.

## Decision

### A. Library: tinyosc, embedded

Vendor [`mhroth/tinyosc`](https://github.com/mhroth/tinyosc) under
`third_party/tinyosc/`. Two files (`tinyosc.c`, `tinyosc.h`),
ISC-licensed, ~500 LOC of C99. Already MSVC-aware (`_WIN32` paths, uses
`htonl`, `strncpy_s` with `_TRUNCATE`). Stateless writer: each message
composes into a caller-owned buffer; tinyosc has no transport coupling.

`LICENSE` and `ATTRIBUTION.md` shipped alongside the source under the
same directory. We compile `tinyosc.c` as C (project gains `LANGUAGES C
CXX`); the header has `extern "C"` for our consumer to call.

### Library candidates considered

| Library | License | LOC | Footprint | Verdict |
|---|---|---|---|---|
| **tinyosc** | ISC | ~500 (2 files) | Two-file embed | **Picked.** Smallest credible footprint; MSVC-ready; matches send-only need. |
| oscpp | MIT-ish | ~1400 (7 headers) | Header-only | Strong runner-up; larger and more abstract for no functional gain over tinyosc here. |
| oscpack | MIT | ~6200 (27 files) | Bundles its own UDP socket + receive subsystem + timer thread | Overkill. Drags in machinery we'd never call. |

### B. Wire layout: send-only UDP, single-message-per-packet

We send one OSC message per UDP packet. No bundles, no time tags. Bundles
would compress per-tick traffic but receivers handle individual messages
fine and the simplicity wins. Future ADRs can revisit if a specific
receiver demands bundles.

### C. Address schema: mirror the shader uniform contract

Addresses match the names in
[`shader_contract.h`](../../src/output/shader_contract.h) under the
`/listeningway/` prefix. This gives shader authors and OSC subscribers
the same mental model:

| OSC address | Type tag | Shape |
|---|---|---|
| `/listeningway/volume` | `f` | scalar |
| `/listeningway/volumeleft` | `f` | scalar |
| `/listeningway/volumeright` | `f` | scalar |
| `/listeningway/volume_norm` | `f` | scalar |
| `/listeningway/volume_att` | `f` | scalar |
| `/listeningway/bass_norm` / `mid_norm` / `treb_norm` | `f` | scalar |
| `/listeningway/bass_att` / `mid_att` / `treb_att` | `f` | scalar |
| `/listeningway/audiopan` | `f` | scalar [-1, +1] |
| `/listeningway/audioformat` | `f` | scalar (channel count) |
| `/listeningway/numbands` | `f` | scalar |
| `/listeningway/freqbands` | `fff…` | array, `numbands` floats |
| `/listeningway/direction8` | `ffffffff` | array of 8 |
| `/listeningway/beat` | `f` | scalar |
| `/listeningway/beat_phase` | `f` | scalar [0, 1) |
| `/listeningway/tempo_bpm` | `f` | scalar |
| `/listeningway/tempo_confidence` | `f` | scalar |
| `/listeningway/phase_volume` / `phase_bass` / `phase_treble` | `f` | scalar |
| `/listeningway/spectral_centroid` | `f` | scalar |
| `/listeningway/loudness` | `f` | scalar |
| `/listeningway/test` | `f` (1.0) | overlay test-button only |

22 messages per tick at the typical settings — ~1.3 K packets/sec at
60 Hz, each ≤300 bytes. Trivial bandwidth on localhost.

Arrays are encoded as repeated `f` type tags (e.g. `,fffff…f`) rather
than the optional OSC `[ ]` bracket syntax. Receivers (TouchDesigner,
Resolume, VRChat, our reference Python receiver) parse both
equivalently; tinyosc doesn't emit brackets so we follow its lead.

### D. Defaults

| Setting | Default | Rationale |
|---|---|---|
| `enabled` | `false` | All toggleable network consumers are off by default (ADR-0010). |
| `host` | `127.0.0.1` | Localhost. LAN exposure is opt-in by user-typed address. |
| `port` | `9000` | TouchDesigner default; common across the ecosystem. |
| `rate_hz` | `60` | Matches DSP publish cadence; small-packet UDP at 60 Hz is comfortable. |

`rate_hz` is clamped to `[1, 120]` at the worker thread.

### E. Threading

A dedicated `std::thread` per `OscConsumer`. The worker owns its UDP
socket strictly thread-locally; no other thread touches it. The worker
polls `AudioSystem::snapshot()` (wait-free seqlock) at `rate_hz` and
fires the messages.

Live settings updates: the worker reads `Store::version()` each tick. If
bumped, it picks up the new `rate_hz` immediately; `host` and `port`
changes require a stop/start cycle (toggling the consumer off and on
from the overlay). This matches the cost-vs-complexity tradeoff for a
debug-friendly first cut; rebinding mid-flight is a future refinement.

### F. Test path: `samples/osc_receiver.py`

A single-file, stdlib-only Python receiver shipped under `samples/`.
Usage: `python samples/osc_receiver.py` — listens on `127.0.0.1:9000`
and prints every incoming message. Lets users verify connectivity
without installing third-party OSC tools.

The overlay also exposes a "Send test packet" button that fires a
single `/listeningway/test  f  1.0` message synchronously (separate
short-lived socket) so users can confirm the destination is reachable
before flipping the main toggle.

## Consequences

### Positive

- **Smallest credible vendoring footprint** for OSC: 2 files + LICENSE +
  ATTRIBUTION. Easy to audit, easy to update, no portfile authoring.
- **Address schema is discoverable**: anyone reading
  `shader_contract.h` knows the OSC tree without consulting a separate
  map.
- **Send-only design has zero anti-cheat exposure** beyond what we
  already have (no listening port, no DLL injection, no kernel calls).
- **Ecosystem reach**: TouchDesigner, Resolume, vvvv, Max/MSP all
  consume this with no extra plumbing on the user's side.

### Negative

- **No bundles** means a slight per-tick UDP overhead (22 packets ×
  small headers). Negligible at localhost; relevant only if someone
  routes the stream over a constrained WAN, which is out of scope for
  v2.x.
- **Live `host` / `port` changes require toggle-off-then-on.** The
  alternative — rebind the socket on every settings version bump —
  trades reliability for ergonomics; we picked reliability.
- **Vendored library warnings**: tinyosc trips a handful of `/W4`
  signed/unsigned mismatch warnings. We relax the level for that one
  file (`COMPILE_OPTIONS /W3`) rather than patch upstream — keeps
  attribution clean.

### Neutral

- **No receive path.** OSC is a bidirectional protocol; we send only.
  An incoming-message path could land later (e.g. for shader-author
  control surfaces) but is not currently asked for.

## Alternatives considered

### Hand-roll the OSC encoder

**Rejected.** OSC has subtle padding rules (every block padded to 4
bytes with NULs), big-endian wire format, and a few corner cases around
type tags that are easy to get subtly wrong. Vendoring 500 lines of
debugged C is the cheaper path. The user explicitly endorsed this:
"if there's already a battle-tested code that we can embed (and
properly credit), then that's our pick."

### vcpkg overlay port

**Rejected** for tinyosc specifically. Two-file embed is more honest
than wrapping a portfile around it. Reserved for the OpenRGB cppSDK
which has multi-file structure (see ADR-0012).

### Send everything as one big bundle

**Rejected for v1.** Bundles compose better when the consumer wants
atomic snapshot semantics ("these N messages all describe the same
moment"), but our receivers tolerate small-packet streams and bundles
cost a per-tick allocation we'd rather skip. Reconsider if a specific
receiver complains.

### Send only a configurable subset

**Rejected for v1.** Filtering the message tree is genuinely useful at
the edges (someone wants only `volume` and `beat` to keep their
receiver fast), but adds settings UX complexity. v1 sends everything;
v2.x can add a per-message-tree toggle if anyone asks.

### TCP transport

**Rejected.** OSC is conventionally UDP; receivers expect that. TCP
adds ordered-delivery and head-of-line guarantees we don't need (we'd
rather drop a stale message than block the worker thread to redeliver
it), and it tweaks the anti-cheat surface (every TCP connection is a
distinct socket lifetime). UDP send-only is the right fit.

## References

- ADR-0010 — `IOutputConsumer` abstraction; vendoring policy; security
  stance for toggleable network consumers.
- [tinyosc on GitHub](https://github.com/mhroth/tinyosc)
- OSC 1.0 specification (1997), [opensoundcontrol.org](http://opensoundcontrol.org/spec-1_0.html)
- [`samples/osc_receiver.py`](../../samples/osc_receiver.py) — debug receiver.
