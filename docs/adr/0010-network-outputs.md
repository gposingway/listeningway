# ADR-0010: Network outputs and the `IOutputConsumer` abstraction

## Status

Accepted — 2026-05-02

Supersedes the consumer-side scope limitation in ADR-0003 (adapter usage policy).

## Context

ADR-0002 named "Consumers" as the fifth pipeline layer (Source → Ring → DSP → Snapshot → Consumers). ADR-0003 deliberately did *not* model consumers as an adapter family because v1 had only two — the ReShade `UniformPublisher` and the ImGui `Overlay` — and both were always-on, render-thread-bound concrete types. Adding an adapter for a population of two would have been speculative scaffolding.

The v2.x roadmap, validated by [research-notes-process-audio.md](research-notes-process-audio.md) and the broader user-wishlist research, queues at least three more output destinations:

- **OSC out** (UDP) — feed TouchDesigner / Resolume / VRChat-adjacent creative-coding tools.
- **OpenRGB client** (TCP) — drive RGB peripherals through the user's existing OpenRGB server.
- **SSE / HTTP server** (TCP listen) — push JSON snapshot events to browser-based companion visualizers and OBS browser sources.

These three are unlike the existing two in three concrete ways:

1. **User-toggleable**, off-by-default. ReShade uniforms and the overlay are always-on by definition — the addon's purpose. Network outputs open ports / sockets / external destinations and must be opt-in.
2. **Worker-thread driven**, not render-thread driven. They publish at a configured rate independent of the game's frame rate.
3. **Externally observable surface** with security implications (anti-cheat heuristics on listen sockets, LAN exposure if misconfigured).

Continuing to bolt these in as concrete types would mean per-consumer ad-hoc lifecycle code in `listeningway_addon.cpp`, no shared toggle UI logic, and no obvious place for the reusable parts (rate limiting, reconnect/backoff, status reporting). Time to revisit ADR-0003's bound.

## Decision

### A. Introduce `IOutputConsumer`

A lifecycle-only adapter. All consumers — internal always-on and toggleable network — implement it.

```cpp
namespace lw::output {

class IOutputConsumer {
public:
    virtual ~IOutputConsumer() = default;

    // Identity
    virtual std::string_view id() const = 0;            // settings key, e.g. "uniform_publisher", "osc"
    virtual std::string_view display_name() const = 0;  // overlay label

    // True if user can toggle on/off via the overlay; false for always-on internal consumers.
    // Toggleable consumers honor settings.network.<id>.enabled.
    virtual bool is_user_configurable() const = 0;

    // Lifecycle. Internal consumers register/unregister ReShade event hooks here.
    // Network consumers spin up / tear down their worker thread + sockets here.
    // Returns false on failure; the orchestrator surfaces the error in the overlay.
    virtual bool start(AudioSystem& system, HMODULE addon_module) = 0;
    virtual void stop() = 0;

    // Optional one-line status for the overlay
    // ("listening on 127.0.0.1:8765 — 2 connected", "OSC sending to host:9000 @ 60 Hz", etc.).
    virtual std::string status_line() const { return {}; }
};

}  // namespace lw::output
```

**Crucial omission**: there is no shared `consume(snapshot)` method. Each consumer chooses its own data acquisition pattern:

| Consumer | Acquisition | Cadence |
|---|---|---|
| `UniformPublisherConsumer` | Pulls from `SeqlockSnapshot` inside `reshade_begin_effects` | Render thread |
| `OverlayConsumer` | Pulls inside `register_overlay` callback | Render thread |
| `OscConsumer` | Worker thread polls `SeqlockSnapshot` at `network.osc.rate_hz` | Configured (default 60 Hz) |
| `SseConsumer` | Worker thread polls `SeqlockSnapshot` at `network.sse.rate_hz` | Configured (default 30 Hz; JSON is bigger) |
| `OpenRgbConsumer` | Worker thread polls `SeqlockSnapshot` at `network.openrgb.rate_hz` | Configured (default 30 Hz; OpenRGB server has known wake-up issues above ~60 Hz) |

That heterogeneity is the *reason* a uniform `consume()` would be wrong. Lifecycle-only is the correct abstraction layer. Snapshot acquisition is wait-free read on `SeqlockSnapshot` regardless of caller; no consumer can ever back-pressure the DSP thread.

### B. Orchestrator: `ConsumerRegistry`

A thin coordinator owning the consumer list:

```cpp
class ConsumerRegistry {
public:
    void add(std::unique_ptr<IOutputConsumer>);
    void start_all(AudioSystem&, HMODULE);  // start internal unconditionally; toggleable per settings
    void stop_all();
    void on_settings_changed(const Settings&);  // toggle consumers as the user flips switches
};
```

`start_all` walks the list. Internal consumers (`is_user_configurable() == false`) start unconditionally. Toggleable consumers consult `settings.network.<id>.enabled`. `on_settings_changed` reacts to overlay toggles by calling `start()` / `stop()` on the affected consumer; same atomic-version-counter pattern the DSP thread uses for live setting hot-reload.

### C. Refactor existing consumers under the interface — no behavior change

`UniformPublisherConsumer` wraps `publish_uniforms()` and registers/unregisters the `reshade_begin_effects` event in `start()`/`stop()`. `OverlayConsumer` wraps `draw_overlay()` and registers/unregisters the overlay callback. Both return `false` from `is_user_configurable()`; both run with the same code paths they do today. The refactor lands as a green-tested PR before any new adapter touches the codebase.

### D. Vendoring policy for network protocol implementations

For protocols with non-trivial wire format (version negotiation, TLV / variable-length payloads, padding rules), **vendor a battle-tested upstream library rather than hand-rolling**. Specifically:

1. **Read the upstream source directly** to understand the protocol and integration shape — this is a one-time learning cost that beats any spec doc.
2. **Prefer permissively-licensed (MIT / BSD / ISC) C++ libraries with no heavy dependencies** (no Qt, no Boost) so the vendored code fits our `x64-windows-static` triplet and our compile flags (`/W4 /permissive- /Zc:preprocessor /Zc:__cplusplus`).
3. **Vendoring shape per library:**
   - **vcpkg overlay port** for multi-file libraries with stable upstream — keeps integration consistent with our existing dependencies, updates via baseline bumps.
   - **Direct embed under `third_party/`** for single-file libraries (one `.c` + one `.h`) — embedding is more honest than a portfile around two files.
   - **No git submodules**: requires `--recursive` clones and complicates CI. Worse than either alternative.
4. **Specific library picks land in their own ADRs** (ADR-0011 for OSC, ADR-0012 for OpenRGB) after a deep-read pass on the candidates.

Hand-rolled wire code is reserved for protocols where vendoring isn't justified — small enough and stable enough that the upstream value-add is below noise (e.g., a fixed-format internal protocol).

### E. Security stance for toggleable network consumers

Toggleable network consumers must:

1. **Default to off.** Every fresh `Listeningway.json` ships with `network.<id>.enabled = false`.
2. **Bind to `127.0.0.1` by default for any listening socket.** Never default to `0.0.0.0`. LAN exposure is opt-in via explicit user configuration.
3. **Surface a security warning in the overlay section** before the toggle. Wording differs by risk profile:
   - **Send-only consumers (OSC, OpenRGB)**: "Sends audio analysis to the configured destination. No port is opened. Safe in any anti-cheat context. Changing the destination to a non-localhost address sends data over the network."
   - **Listening consumers (SSE)**: "Opens an HTTP server inside the game process on `127.0.0.1:<port>`. Local processes (browsers, scripts) can connect. Disable for competitive or anti-cheat-protected titles."
4. **Honor lifetime correctness on every error path.** Connection / socket / worker-thread cleanup goes through a single RAII path. Addon unload tears everything down before the listener / sender closes. SSE per-client connections close on client disconnect, write failure, TCP keepalive timeout, or parent shutdown — all routed through one connection destructor.
5. **Bounded per-client queues with drop-oldest policy** (SSE in particular). A wedged client must never back-pressure the publisher.
6. **Rate limit independently of the DSP publish rate.** Configurable `rate_hz` per consumer, with sensible default per protocol (60 Hz for OSC, 30 Hz for SSE, 30 Hz for OpenRGB).
7. **Provide live status in the overlay**, including connection counts and the last error if `start()` failed (port in use, server unreachable, etc.). No silent failures.

## Consequences

### Positive

- **The consumer side now scales to N implementations behind one contract.** Adding a future consumer (DMX, MIDI, Hue, custom) is a self-contained PR.
- **Behavioral parity for existing consumers.** The refactor is mechanical and tested before any new code lands.
- **Reduced wire-protocol risk.** OpenRGB version negotiation, OSC bundle/padding rules, and other corner cases are inherited from upstream libraries that have already debugged them across thousands of users.
- **Consistent UX for network outputs.** Toggle, status line, security warning, rate slider all use shared overlay helpers — no per-consumer one-off layout.
- **Clean security boundary.** All listening sockets bound to localhost by default; LAN exposure is explicit user opt-in with documented risk.

### Negative

- **One more interface to teach in CONTRIBUTING.md.** The "How to add" recipe gains a new section. Cost is small; the abstraction is intentionally narrow (5 methods, no data delivery).
- **Vendored libraries add CI surface.** Each vcpkg overlay port or `third_party/` embed needs to be kept current. Mitigation: pin specific upstream commits, document update procedure in CONTRIBUTING.md.
- **The `start()` / `stop()` lifecycle requires care under live toggling.** A user flipping a network toggle rapidly should not produce socket leaks or zombie threads. Standard cure: state machine inside each consumer (Off → Starting → Running → Stopping → Off | Error) — same pattern AudioSystem uses for capture lifecycle.

### Neutral

- **`SeqlockSnapshot` semantics are unchanged.** All consumers still observe wait-free reads; the publisher is unaffected by consumer count or behavior.
- **Per-consumer worker threads add ~3 OS threads if all three network consumers are enabled simultaneously.** Negligible memory and scheduling cost on any modern desktop.

## Alternatives considered

### Single shared `consume(snapshot)` method on `IOutputConsumer`

**Rejected.** Render-thread consumers (uniforms, overlay) want to *pull* on their event handlers; worker-thread consumers (network) want to *push* at a configured cadence. Forcing both through the same call signature would require either ignoring the caller's thread (unsafe — render-thread callbacks can't `send()`) or adding a "where am I being called from?" parameter (smell). Heterogeneity is the correct property to express, not paper over. Lifecycle-only avoids the whole question.

### Two separate interfaces: `IInternalConsumer` and `IExternalConsumer`

**Rejected.** They share enough plumbing (registration, lifecycle, status reporting, error surfacing) that splitting forces parallel orchestrator code. A single interface with a `is_user_configurable()` discriminator is simpler.

### Hand-roll all wire protocols in-house

**Rejected.** OpenRGB's protocol-version handshake is documented as memory-corrupting if mishandled. OSC has subtle padding and bundle rules. SSE is the simplest of the three (text protocol over HTTP/1.1) and could go either way. Vendoring the first two is a clear win; SSE is a per-case judgment call captured in its own future ADR.

### Skip the refactor; bolt new consumers on as concrete types

**Rejected.** The third concrete consumer is the point at which "just add another concrete one" stops being cheaper than the abstraction. We're committing to at least two more (OSC and OpenRGB) and likely a third (SSE). The refactor is the right move at this scale and stays a behavioral no-op.

### Use git submodules for vendored libraries

**Rejected.** Submodules complicate the contributor experience (clone with `--recursive`, `submodule update --init`, dirty-state confusion when the submodule pointer drifts). vcpkg overlay ports and direct embed both keep the contributor flow as `prepare.bat` → `build.bat`.

## References

- ADR-0002 — pipeline architecture; Consumers is the fifth layer.
- ADR-0003 — adapter usage policy; this ADR supersedes its consumer-side scope limitation.
- ADR-0009 — `ProcessAudioSource`; same security-stance pattern (off-by-default, OS-floor caveat) applied here.
- [research-notes-process-audio.md](research-notes-process-audio.md) — established the in-process / out-of-process consumer split.
- Future: ADR-0011 (OSC consumer + library pick) and ADR-0012 (OpenRGB consumer + cppSDK vendoring).
