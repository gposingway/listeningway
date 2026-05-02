# ADR-0010: Network outputs and the `IOutputConsumer` abstraction

## Status

Accepted, 2026-05-02.

Supersedes the consumer-side scope limitation in ADR-0003 (adapter usage policy).

## Context

ADR-0002 named "Consumers" as the fifth pipeline layer (Source → Ring → DSP → Snapshot → Consumers). ADR-0003 deliberately did *not* model consumers as an adapter family because v1 had only two of them (the ReShade `UniformPublisher` and the ImGui `Overlay`), and both were always-on, render-thread-bound concrete types. Adding an adapter for a population of two would have been speculative scaffolding.

The v2.x roadmap, validated by [research-notes-process-audio.md](research-notes-process-audio.md) and the broader user-wishlist research, queues at least three more output destinations:

- An OSC sender over UDP, to feed TouchDesigner, Resolume, and the VRChat-adjacent creative-coding tools.
- An OpenRGB client over TCP, to drive RGB peripherals through the user's existing OpenRGB server.
- An SSE / HTTP server, to push JSON snapshot events to browser-based companion visualizers and OBS browser sources.

These three differ from the existing two in three concrete ways:

1. They are user-toggleable and off by default. ReShade uniforms and the overlay are always on; that's the addon's purpose. Network outputs open ports, sockets, or external destinations and must be opt-in.
2. They are worker-thread driven, not render-thread driven. They publish at a configured rate independent of the game's frame rate.
3. They have an externally observable surface with security implications: anti-cheat heuristics on listen sockets, LAN exposure if misconfigured.

Continuing to bolt these in as concrete types would mean per-consumer ad-hoc lifecycle code in `listeningway_addon.cpp`, no shared toggle UI logic, and no obvious place for the reusable parts (rate limiting, reconnect/backoff, status reporting). It is time to revisit the bound ADR-0003 set.

## Decision

### A. Introduce `IOutputConsumer`

A lifecycle-only adapter. Both flavours of consumer (the internal always-on pair and the toggleable network family) implement it.

```cpp
namespace lw::output {

class IOutputConsumer {
public:
    virtual ~IOutputConsumer() = default;

    // Identity
    virtual std::string_view id() const = 0;            // settings key, e.g. "uniform_publisher", "osc"
    virtual std::string_view display_name() const = 0;  // overlay label

    // True if the user can toggle the consumer on or off via the overlay;
    // false for always-on internal consumers. Toggleable consumers honour
    // settings.network.<id>.enabled.
    virtual bool is_user_configurable() const = 0;

    // Lifecycle. Internal consumers register and unregister ReShade event
    // hooks here. Network consumers spin up and tear down their worker
    // thread and sockets here. Returns false on failure; the orchestrator
    // surfaces the error in the overlay.
    virtual bool start(AudioSystem& system, HMODULE addon_module) = 0;
    virtual void stop() = 0;

    // Optional one-line status for the overlay
    // (e.g. "listening on 127.0.0.1:8765, 2 connected" or
    //       "OSC sending to host:9000 @ 60 Hz").
    virtual std::string status_line() const { return {}; }
};

}  // namespace lw::output
```

There is deliberately no shared `consume(snapshot)` method. Each consumer chooses how it acquires data:

| Consumer | Acquisition | Cadence |
|---|---|---|
| `UniformPublisherConsumer` | Pulls from `SeqlockSnapshot` inside `reshade_begin_effects` | Render thread |
| `OverlayConsumer` | Pulls inside the `register_overlay` callback | Render thread |
| `OscConsumer` | Worker thread polls `SeqlockSnapshot` at `network.osc.rate_hz` | Configurable, default 60 Hz |
| `SseConsumer` | Worker thread polls `SeqlockSnapshot` at `network.sse.rate_hz` | Configurable, default 30 Hz (JSON is bigger) |
| `OpenRgbConsumer` | Worker thread polls `SeqlockSnapshot` at `network.openrgb.rate_hz` | Configurable, default 30 Hz (the OpenRGB server has wake-up issues above ~60 Hz) |

That heterogeneity is the reason a uniform `consume()` would be wrong. Lifecycle-only is the correct abstraction layer. Snapshot acquisition is a wait-free read on `SeqlockSnapshot` regardless of the caller, so no consumer can ever back-pressure the DSP thread.

### B. Orchestrator: `ConsumerRegistry`

A thin coordinator that owns the consumer list:

```cpp
class ConsumerRegistry {
public:
    void add(std::unique_ptr<IOutputConsumer>);
    void start_all(AudioSystem&, HMODULE);  // start internal unconditionally; toggleable per settings
    void stop_all();
    void on_settings_changed(const Settings&);  // toggle consumers as the user flips switches
};
```

`start_all` walks the list. Internal consumers (where `is_user_configurable() == false`) start unconditionally. Toggleable consumers consult `settings.network.<id>.enabled`. `on_settings_changed` reacts to overlay toggles by calling `start()` or `stop()` on the affected consumer, using the same atomic-version-counter pattern the DSP thread uses for live setting hot-reload.

### C. Refactor existing consumers under the interface (no behaviour change)

`UniformPublisherConsumer` wraps `publish_uniforms()` and registers and unregisters the `reshade_begin_effects` event in `start()` and `stop()`. `OverlayConsumer` wraps `draw_overlay()` and registers and unregisters the overlay callback. Both return `false` from `is_user_configurable()`; both run with the same code paths they do today. The refactor lands as a green-tested PR before any new adapter touches the codebase.

### D. Vendoring policy for network protocol implementations

For protocols with awkward wire format (version negotiation, TLV or variable-length payloads, padding rules), we vendor a mature upstream library rather than hand-rolling.

1. Read the upstream source directly to understand the protocol and integration shape. That is a one-time learning cost that beats any specification document.
2. Prefer permissively-licensed C++ libraries (MIT, BSD, ISC) with no heavy dependencies (no Qt, no Boost) so the vendored code fits our `x64-windows-static` triplet and our compile flags (`/W4 /permissive- /Zc:preprocessor /Zc:__cplusplus`).
3. Vendoring shape per library:
   - vcpkg overlay port for multi-file libraries with stable upstream. This keeps integration consistent with our existing dependencies and updates via baseline bumps.
   - Direct embed under `third_party/` for single-file libraries (one `.c` plus one `.h`). Embedding two files is more honest than wrapping a portfile around them.
   - No git submodules. They require `--recursive` clones, complicate CI, and are worse than either alternative.
4. Specific library picks land in their own ADRs (ADR-0011 for OSC, ADR-0012 for OpenRGB) after a deep-read pass on the candidates.

Hand-rolled wire code is reserved for protocols where vendoring isn't justified, meaning small enough and stable enough that the upstream value-add is below noise (for example, a fixed-format internal protocol).

### E. Security stance for toggleable network consumers

Toggleable network consumers must:

1. Default to off. Every fresh `Listeningway.json` ships with `network.<id>.enabled = false`.
2. Bind to `127.0.0.1` by default for any listening socket. Never default to `0.0.0.0`. LAN exposure is opt-in via explicit user configuration.
3. Surface a security warning in the overlay section before the toggle. Wording differs by risk profile:
   - Send-only consumers (OSC, OpenRGB): "Sends audio analysis to the configured destination. No port is opened. Safe in any anti-cheat context. Changing the destination to a non-localhost address sends data over the network."
   - Listening consumers (SSE): "Opens an HTTP server inside the game process on `127.0.0.1:<port>`. Local processes (browsers, scripts) can connect. Disable for competitive or anti-cheat-protected titles."
4. Honour lifetime correctness on every error path. Connection, socket, and worker-thread cleanup goes through a single RAII path. Addon unload tears everything down before the listener or sender closes. SSE per-client connections close on client disconnect, write failure, TCP keepalive timeout, or parent shutdown, all routed through one connection destructor.
5. Use bounded per-client queues with a drop-oldest policy (SSE in particular). A wedged client must never back-pressure the publisher.
6. Rate-limit independently of the DSP publish rate. Configurable `rate_hz` per consumer, with a sensible default per protocol (60 Hz for OSC, 30 Hz for SSE, 30 Hz for OpenRGB).
7. Provide live status in the overlay, including connection counts and the last error if `start()` failed (port in use, server unreachable, and so on). No silent failures.

## Consequences

### Positive

- The consumer side now scales to N implementations behind one contract. Adding a future consumer (DMX, MIDI, Hue, custom) is a self-contained PR.
- The refactor leaves existing consumers behaviourally identical and is tested before any new code lands.
- Wire-protocol risk drops. OpenRGB version negotiation, OSC bundle and padding rules, and similar corner cases are inherited from upstream libraries that have already debugged them across thousands of users.
- Network outputs share UX: toggle, status line, security warning, and rate slider all use the same overlay helpers, with no per-consumer one-off layout.
- The security boundary is clean. All listening sockets bind to localhost by default; LAN exposure is an explicit opt-in with a documented risk.

### Negative

- One more interface to teach in CONTRIBUTING.md. The "How to add" recipe gains a new section. The cost is small; the abstraction is intentionally narrow (five methods, no data delivery).
- Vendored libraries add CI surface. Each vcpkg overlay port or `third_party/` embed has to be kept current. Mitigation: pin specific upstream commits and document the update procedure in CONTRIBUTING.md.
- The `start()` / `stop()` lifecycle needs care under live toggling. A user flipping a network toggle rapidly must not produce socket leaks or zombie threads. The standard cure is a state machine inside each consumer (Off → Starting → Running → Stopping → Off | Error), the same pattern AudioSystem uses for capture lifecycle.

### Neutral

- `SeqlockSnapshot` semantics are unchanged. All consumers still observe wait-free reads; the publisher is unaffected by consumer count or behaviour.
- Per-consumer worker threads add about three OS threads when all three network consumers are enabled simultaneously. Memory and scheduling cost on any modern desktop is negligible.

## Alternatives considered

### Single shared `consume(snapshot)` method on `IOutputConsumer`

Rejected. Render-thread consumers (uniforms, overlay) want to pull on their event handlers; worker-thread consumers (network) want to push at a configured cadence. Forcing both through the same call signature would require either ignoring the caller's thread (unsafe, since render-thread callbacks can't `send()`) or adding a "where am I being called from?" parameter (a smell). Heterogeneity is the correct property to express, not paper over. Lifecycle-only avoids the whole question.

### Two separate interfaces: `IInternalConsumer` and `IExternalConsumer`

Rejected. They share enough plumbing (registration, lifecycle, status reporting, error surfacing) that splitting forces parallel orchestrator code. A single interface with an `is_user_configurable()` discriminator is simpler.

### Hand-roll all wire protocols in-house

Rejected. OpenRGB's protocol-version handshake is documented as memory-corrupting if mishandled. OSC has subtle padding and bundle rules. SSE is the simplest of the three (a text protocol over HTTP/1.1) and could go either way. Vendoring the first two is a clear win; SSE is a per-case judgement call captured in its own future ADR.

### Skip the refactor; bolt new consumers on as concrete types

Rejected. The third concrete consumer is the point at which "just add another concrete one" stops being cheaper than the abstraction. We are committing to at least two more (OSC and OpenRGB) and likely a third (SSE). The refactor is the right move at this scale and stays a behavioural no-op.

### Use git submodules for vendored libraries

Rejected. Submodules complicate the contributor experience: clone with `--recursive`, `submodule update --init`, dirty-state confusion when the submodule pointer drifts. vcpkg overlay ports and direct embed both keep the contributor flow as `prepare.bat` then `build.bat`.

## References

- ADR-0002, pipeline architecture; Consumers is the fifth layer.
- ADR-0003, adapter usage policy; this ADR supersedes its consumer-side scope limitation.
- ADR-0009, `ProcessAudioSource`; the same security-stance pattern (off-by-default, OS-floor caveat) is applied here.
- [research-notes-process-audio.md](research-notes-process-audio.md), which established the in-process versus out-of-process consumer split.
- Future: ADR-0011 (OSC consumer and library pick) and ADR-0012 (OpenRGB consumer and cppSDK vendoring).
