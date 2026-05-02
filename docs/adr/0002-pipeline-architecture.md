# ADR-0002: Five-layer pipeline architecture

## Status

Accepted, 2026-05-01

## Context

ADR-0001 commits to a clean-room v2 engine. This ADR fixes the structural shape of that engine: the layers, what each layer is responsible for, and how data flows between them.

The v1 codebase tightly couples capture and analysis (the WASAPI thread synchronously calls into `AudioAnalyzer::AnalyzeAudioBuffer` while holding a global mutex). It also tightly couples analysis and presentation (the overlay reads `g_audio_data` under the same mutex). This means a WASAPI hiccup affects analysis correctness, an analysis change affects capture latency, and the overlay's read frequency competes with the capture thread's write frequency for a single mutex. None of these couplings are intentional; they exist because there are no boundaries between the concerns.

The validating research (see [research-notes.md](research-notes.md) §3) confirmed that production-quality real-time audio software (JUCE, VST3, FAUST, JUCE `AudioProcessorGraph`) consistently structures itself as a data-flow pipeline with explicit thread boundaries and lock-free or wait-free buffer/snapshot publication.

## Decision

The Listeningway v2 engine is structured as a **five-layer data-flow pipeline**:

```
┌───────────────┐  raw float frames   ┌──────────────┐
│  IAudioSource │ ──────────────────► │  FrameRing   │
│  (adapter)    │      (push)         │  (SPSC)      │
└───────────────┘                     └──────┬───────┘
                                             │ pop
                                             ▼
                          ┌──────────────────────────────────┐
                          │  Pipeline (sequence of IDspStage) │
                          │  Volume → FFT → Bands → EQ → ...  │
                          │  operates on AnalysisFrame        │
                          └──────────────┬───────────────────┘
                                         │ publish (seqlock)
                                         ▼
                                  ┌──────────────┐
                                  │  Snapshot    │
                                  │  (immutable) │
                                  └──────┬───────┘
                                         │ read (lock-free)
                          ┌──────────────┴──────────────┐
                          ▼                             ▼
                  ┌──────────────┐              ┌──────────────┐
                  │  ImGui       │              │  ReShade     │
                  │  overlay     │              │  uniforms    │
                  └──────────────┘              └──────────────┘
                                + future read-only consumers
```

### Layer 1. Source (adapter)

**Responsibility:** produce float audio frames from any origin (WASAPI loopback, process loopback, file, signal generator, no-op). Push frames into the ring via a `FrameSink` callback. Report capabilities (sample rate, channels, channel layout, frame size, latency) at open time.

**Threading:** runs on a dedicated capture thread. Hard real-time. Never allocates, locks, or calls into upper layers beyond the sink callback.

**Adapter interface:** `IAudioSource` (see ADR-0003).

### Layer 2. Ring (concrete, no adapter)

**Responsibility:** lock-free single-producer / single-consumer queue between capture and DSP threads. Power-of-two capacity (~1 second of stereo float audio at 48 kHz ≈ 96 KB).

**Implementation:** `moodycamel::ReaderWriterQueue` (header-only, BSD; vendored). Justification in [research-notes.md](research-notes.md) §6.

**Threading:** producer = capture thread (Layer 1); consumer = DSP thread (Layer 3). Memory orderings: relaxed on producer's own index, acquire on the other side's index, release on publish. Cache-line padding (`alignas(64)`) on indices to prevent false sharing.

### Layer 3. DSP Pipeline (adapter sequence)

**Responsibility:** transform `AnalysisFrame` through a sequence of stages. Each stage is a small unit of analysis (volume, FFT, bands, equalizer, flux, beat, pan, directional, spectral centroid). Stages declare what they read and what they write; the pipeline is composed at startup.

**Threading:** runs on a single dedicated DSP thread. Drains the ring in batches, processes each `AnalysisFrame` through all stages, publishes the resulting snapshot. Soft real-time (target ≤ 5 ms per frame).

**Data carrier:** `AnalysisFrame` is a struct of `std::optional<...>` fields. Each field represents a feature that *can* be computed; a field is `std::nullopt` when its stage was disabled or skipped (not "not yet computed"). Stages that ran always populate their outputs.

**Adapter interface:** `IDspStage` (see ADR-0003). Beat detection is a sub-adapter `IBeatDetector` consumed by `BeatStage`.

**Pipeline composition:** at startup the pipeline is assembled by appending stages. The default order is the visual diagram above. Stage order matters because later stages can read earlier stages' outputs (e.g. `BandsStage` reads magnitudes produced by `FftStage`). The pipeline validates dependencies at startup and refuses to start if a stage's required inputs aren't produced by any earlier stage.

### Layer 4. Snapshot (concrete, no adapter)

**Responsibility:** publish the latest analysis result atomically to all consumers. One writer (the DSP thread); N readers (overlay, uniform publisher, future consumers).

**Implementation:** seqlock with `memcpy`-based payload copy. Payload is a POD `AudioSnapshot` struct with fixed-size arrays (`std::array<float, kMaxBands>` plus a `uint32_t band_count`). **no `std::vector`**, no heap allocation in the publish path. See [research-notes.md](research-notes.md) §6 for the writer/reader pseudocode and memory orderings.

**Threading:** writer = DSP thread; readers = any thread, any frequency. Wait-free for the writer. Lock-free for readers (with bounded retry on contention; readers fall back to last good copy after 4 failed reads).

### Layer 5. Consumers (concrete, no adapter)

**Responsibility:** read the snapshot and do something with it.

**Two consumers in v1:**
1. **Uniform publisher**. Runs on ReShade's `reshade_begin_effects` event (~60–300 fps render thread). Reads snapshot, writes shader uniforms.
2. **ImGui overlay**. Runs on ReShade's overlay callback (~60 fps GUI). Reads snapshot, draws debug visualization.

**Future consumers** plug in identically. Call `snapshot_channel.read()` from anywhere. No `IConsumer` interface is needed; consumers are just call sites that hit the snapshot. If we later have many consumers and want a single "publish hook," that's a `std::vector<std::function<void(const AudioSnapshot&)>>` on the system, not an inheritance hierarchy.

## Refinements from research

The pipeline-architecture research surfaced three concrete refinements to the initial sketch (see [research-notes.md](research-notes.md) §3):

1. **`Capabilities` struct returned by `IAudioSource::open()`** carrying sample rate, channels, layout, frame size, reported latency. The DSP pipeline and FFT stage size their internal buffers from this rather than guessing per-frame.
2. **`AnalysisFrame::optional` semantics**: `std::nullopt` means "stage was skipped / disabled," not "stage was lazy / hasn't run yet." When a stage runs, it always populates its outputs. This makes property tests on `AnalysisFrame` straightforward.
3. **No VST3-style sample-accurate parameter automation.** A simple `const Settings&` reference passed into each stage is sufficient. We are an analysis tool, not a synth; we never need to interpolate parameter values across a frame.

## Threading model summary

| Thread | Owner | Responsibilities | Real-time class |
|---|---|---|---|
| Capture | `IAudioSource` impl | Acquire frames from WASAPI / file / generator; push to ring | Hard real-time (never block; MMCSS `L"Audio"`) |
| DSP | `Pipeline` runner | Pop ring; run stages; publish snapshot | Soft real-time (≤ 5 ms / frame) |
| Render | ReShade | Read snapshot; write uniforms | Soft real-time (whatever the game's frame budget allows) |
| Overlay | ReShade | Read snapshot; draw ImGui | Soft real-time |
| Worker | `AudioSystem` | Device-change rebuild, lifecycle transitions | Best-effort |

Worker thread exists because device-change callbacks must be non-blocking (see [research-notes.md](research-notes.md) §2). The callback signals the worker, which performs the actual `IAudioSource::stop` / re-`open` / `start` cycle off-thread.

## Consequences

### Positive

- **Producer/consumer decoupling:** WASAPI hiccups absorbed by the ring; analysis bugs can't starve capture.
- **Zero locks on the audio hot path.** SPSC ring is wait-free; snapshot is wait-free for writer, lock-free for readers.
- **Multiple consumers without coordination.** Uniform publisher and overlay each read independently; future consumers (OSC out, telemetry, recording) plug in with no changes.
- **Each layer is independently testable.** Sources can be mocked; DSP stages are pure functions; snapshot is a value type; consumers are unit-testable.
- **Stage dependency declaration** (a stage states what it reads and writes) catches order-of-composition bugs at startup, not at runtime.

### Negative

- **More files** than v1. Roughly 25–30 small files versus the current 30 large files. Each file has one job.
- **Slight memory overhead.** Ring buffer (~96 KB) + double-snapshot for seqlock + per-stage state. Total well under 1 MB. Negligible.
- **Discipline cost.** Layers must not reach across themselves. Source must not call into DSP. DSP must not write to a setting. Consumers must not call into pipeline lifecycle. This is what makes the model work; it requires reviewer attention.

### Neutral

- **Latency budget is unchanged.** Capture (~10–20 ms WASAPI period) + ring transit (~1 frame) + DSP (~5 ms) + snapshot publish (~10 µs) + render read (~10 µs) ≈ 15–25 ms total. Identical envelope to v1, with much more deterministic structure.

## Alternatives considered

### Direct-call (capture thread runs analysis)
v1's current shape. **Rejected.** Couples WASAPI failure modes into analysis correctness. Provides no isolation point for testing.

### Triple buffer for snapshot publication
Standard audio-library idiom (e.g. JUCE patterns). **Rejected** in favor of seqlock because we have ≥2 concurrent readers (uniform publisher + overlay, plus future) and triple-buffer slot accounting for N readers requires reader-side reservation. Seqlock handles N readers natively. Trade-off: seqlock readers may fall back to last good copy on contention, which is acceptable for visualization (we just show the previous frame). See [research-notes.md](research-notes.md) §6.

### Hand-rolled SPSC ring
**Rejected** in favor of `moodycamel::ReaderWriterQueue` to reduce risk. We are MSVC/x64 only but the moodycamel implementation is stress-tested across many environments and is one header. Hand-rolled would be ~80 LOC we'd own the correctness proof for. Saving that engineering cost for actual feature work.

### `IConsumer` interface
**Rejected.** Consumers are call sites, not an inheritance hierarchy. The interface would buy nothing.

### Compile-time pipeline composition (`std::tuple<VolumeStage, FftStage, ...>` + `std::apply`)
Inlines the entire stage sequence; ~10% perf headroom on the hot loop. **Deferred to v1.5.** The dynamic `std::vector<std::unique_ptr<IStage>>` form is simpler and more amenable to debug tooling (profiler view, "skip this stage" toggles). Compile-time form can be added as an optimization without changing the design.

## References

- [research-notes.md §3](research-notes.md). Pipeline architecture validation against JUCE / VST3 / rtaudio / PortAudio.
- [research-notes.md §6](research-notes.md). Lock-free SPSC ring + seqlock implementation guidance.
- ADR-0003. Adapter usage policy (which interfaces are first-class).
- ADR-0006. Testing strategy that exploits the pipeline structure.
