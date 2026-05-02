# ADR-0003: Adapter pattern usage policy. Three interfaces, no more

## Status

Accepted, 2026-05-01

## Context

The five-layer pipeline (ADR-0002) introduces three places where polymorphism is genuinely useful:
- Audio sources differ in implementation (WASAPI loopback vs file vs signal generator vs no-op) but share lifecycle and contract.
- DSP stages differ in transformation (volume vs FFT vs beat detection) but share the "transform an `AnalysisFrame`" shape.
- Beat detectors differ in algorithm (simple energy vs spectral flux autocorrelation vs future variants) but share input/output.

A frequent design failure in audio software is over-application of the adapter / interface pattern: every concrete type gets an interface "for testability" or "for future flexibility," and the codebase ends up with `IRingBuffer`, `IConfigStore`, `ISnapshotChannel`, `IConsumer`, `IUniformPublisher`, `ILogger`, etc.. Abstractions that have exactly one implementation and exist only as ceremony around their concrete type.

This ADR fixes the policy: **adapters appear at three points only.** Everything else is concrete.

## Decision

### Three adapters, justified

#### `IAudioSource`
Multiple real implementations, each with materially different mechanics:
- `WasapiLoopbackSource`. System audio capture via WASAPI loopback.
- `OffSource`. No-op source for "audio analysis disabled" state. Sink is never called.
- `FileSource`. Reads a WAV file, pushes frames at real-time pace. Used for headless testing and replay-driven bug reports (ADR-0006).
- `SignalGeneratorSource`. Tests only. Synthesizes sine, click train, white noise with deterministic content.
- `ProcessAudioSource` (v1, opt-in). Per-process loopback via Windows 10 build 20348+ APIs.
- `TeeSource` (v1, optional decorator). Wraps another source, writes raw frames to disk while passing through.

The interface earns its keep: each implementation has a different mechanism but they all need to be lifecycle-managed identically by `AudioSystem`. Tests rely on `FileSource` and `SignalGeneratorSource` for determinism. The dropdown UI iterates over registered sources.

#### `IDspStage`
The pipeline (ADR-0002) is a sequence of stages, each transforming an `AnalysisFrame`. Volume, FFT, Bands, Equalizer, LogBoost, Flux, Beat, Pan, Directional, SpectralCentroid, Loudness. Each is a stage. Tests pin behavior per-stage. Profiling instruments per-stage. Future stages plug in without touching unrelated code.

The interface is small (`process(AnalysisFrame&, const Settings&)` plus `name()`, `reads()`, `writes()` for dependency declaration), and its existence enables the compositional pipeline that's the entire point of the architecture.

#### `IBeatDetector`
Already present in v1. Multiple algorithms (`SimpleEnergy`, `SpectralFluxAutocorrelation`) that share the contract `process(magnitudes, flux, dt) → BeatResult`. Different musical genres benefit from different detectors. The current `BeatStage` holds the active `IBeatDetector` and routes work to it.

### Concrete elsewhere. No adapter

| Concrete component | Why no adapter |
|---|---|
| `FrameRing` (ring buffer) | One implementation (`moodycamel::ReaderWriterQueue`). No second variant exists or is planned. |
| `SnapshotChannel` (seqlock publish) | One implementation. The seqlock pattern with `memcpy` body is the right choice; alternatives are worse for our case. |
| `AudioSnapshot` | A POD value type. Not a behavior. |
| `Settings` / `Setting<T>` | Value types with declarative bounds. Validation, marshalling, and UI bind read the same `Setting<T>` object directly. |
| `AudioSystem` | The orchestrator. Single instance, owns everything else. No second universe of "alternative audio systems." |
| `UniformPublisher` | Concrete. Walks `AudioSnapshot` fields and writes to ReShade uniform sources via the contract from ADR-0005. Adding a second consumer is *another concrete consumer*, not a polymorphic publisher. |
| Overlay panels | Concrete. ImGui code that reads the snapshot and draws. |

### When in doubt. Concrete

The default answer to "should this be an interface?" is **no**. An interface is justified when:
1. **Two or more meaningful implementations exist or are concretely planned.** Hypothetical future flexibility doesn't count.
2. **Tests genuinely need a mock.** "We could mock this for tests" is true of every class; the question is whether mocking is materially easier than using the real thing.
3. **The contract is small and stable.** Wide interfaces with many methods are a smell. The abstraction probably doesn't carve at a real joint.

If none of these hold, write a concrete class.

## Consequences

### Positive

- **Three documented extension points.** Adding a new source, stage, or beat detector is well-trodden ground. Anything else is "rebuild and propose a new ADR if you need flexibility."
- **No DI container.** Wiring is a 30-line function in `DllMain` (`register_source(...)`, `pipeline.add(...)`). Fully visible, no magic.
- **No factory hierarchies.** Construction is direct via `std::make_unique<WasapiLoopbackSource>()`.
- **No `IConsumer`, `ISnapshotChannel`, `IConfigStore`**. Concerns that shouldn't be polymorphic stay concrete.
- **Property tests stay simple**. They test concrete stages and concrete sources, not interface contracts.

### Negative

- **No swap-out for components we declared concrete.** If the snapshot publication strategy ever needs to change, that's a refactor in `SnapshotChannel`, not a configuration toggle. Acceptable: the choice is made, alternatives have been considered (ADR-0002, [research-notes.md](research-notes.md) §6).
- **Future-flexibility hypothesis is rejected.** If five years from now someone wants two `RingBuffer` implementations side-by-side, they'll need to introduce the adapter at that point. We refuse to design for it now.

### Neutral

- **Code review must enforce the policy.** New PRs proposing `IRing`, `IConfig`, `IConsumer`, etc. should be rejected with a reference to this ADR unless they meet the three justification criteria above.

## Alternatives considered

### Adapter everywhere ("for testability")
**Rejected.** Tests don't need it for ring buffers, snapshot channels, or config stores. Those are value-like or have one obvious implementation. Adding interfaces "just in case" creates parallel hierarchies (`Ring` + `IRing`, `Settings` + `ISettings`) that obscure the actual data flow.

### Adapter only at the system boundary (one mega-interface for "the whole audio system")
**Rejected.** Defeats the point. The three interfaces we *do* keep are at different layers and serve different purposes. A single mega-interface would force every test to mock the entire system.

### No adapters at all (concrete `WasapiLoopbackSource` referenced directly)
**Rejected.** Then `FileSource` and `SignalGeneratorSource` can't substitute, which kills the testing strategy (ADR-0006). And the runtime "switch source" capability would require a switch statement somewhere, which is an interface in disguise.

## Reference: the three interfaces

```cpp
class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    virtual SourceInfo info() const = 0;
    virtual bool available() const = 0;

    using FrameSink = std::function<void(std::span<const float>, Format)>;
    virtual std::optional<Capabilities> open() = 0;   // returns capabilities if open succeeds
    virtual bool start(FrameSink sink) = 0;
    virtual void stop() = 0;
    virtual bool restart_requested() const = 0;
};

class IDspStage {
public:
    virtual ~IDspStage() = default;
    virtual std::string_view name() const = 0;
    virtual std::span<const FieldId> reads() const = 0;    // for dependency check
    virtual std::span<const FieldId> writes() const = 0;
    virtual void process(AnalysisFrame& frame, const Settings& cfg) = 0;
};

class IBeatDetector {
public:
    virtual ~IBeatDetector() = default;
    virtual std::string_view name() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual BeatResult process(std::span<const float> magnitudes,
                               float flux_total, float flux_low, float dt) = 0;
};
```

(Exact signatures may evolve during implementation; the above is the intent. No other interfaces are introduced without superseding this ADR.)

## References

- ADR-0002. Defines where the three interfaces fit in the pipeline.
- ADR-0006. Testing strategy depends on `IAudioSource` having multiple implementations.
- [research-notes.md §3](research-notes.md). JUCE / VST3 patterns informing the adapter choices.
