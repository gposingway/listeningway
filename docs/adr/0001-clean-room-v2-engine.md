# ADR-0001: Clean-room v2 engine, rebuild rather than refactor

## Status

Accepted, 2026-05-01.

## Context

Listeningway v1 is approximately 5,500 lines across ~30 files. Three recent bugs surfaced during testing of an in-place refactor:

1. The SIMD-toggle path froze frequency bands because of brace nesting in a 1,000-line god-procedure (`AnalyzeAudioBuffer` in `src/audio/analysis/audio_analysis.cpp`).
2. `frequency.logStrength` silently reverted to a clamped value on every load because the validator's range, the default, and the UI slider's range were declared in three different places and disagreed.
3. Provider switching from "Off" back to "System" left the analysis frozen because the lifecycle was coordinated across four atomic flags spread across pipeline, manager, provider, and overlay layers without an explicit state machine.

These are not independent defects. They are symptoms of the same structural problem: there is no boundary between *capture*, *analysis*, *configuration*, and *presentation*. Each fix attempt either fights the existing shape or risks introducing new bugs at seams that don't exist as first-class abstractions.

A separate concern: about 30% of the v1 code carries genuine institutional knowledge (WASAPI quirks, channel-layout heuristics, beat profile presets, default constants tuned by feel, the public shader uniform names). The other 70% is plumbing that exists because of architectural mis-fits.

## Decision

**Rebuild Listeningway as a clean-room v2 engine** rather than continue refactoring v1 in place:

1. **`git mv src src.harvest`.** The v1 source tree is renamed and removed from the build. It remains in the repository as read-only reference material: searchable, not compiled.
2. **`src/` is scaffolded fresh** against the architecture captured in ADR-0002 through ADR-0008.
3. **The v1 to v2 transition is a beta release.** Users accept that:
   - The configuration file format changes (no migration framework in v1; defaults regenerate on first run).
   - Internal behavior may change. Beat detection thresholds, smoothing constants, and AGC time windows are tuned in v2 against current best practice, not against v1's tuned-by-feel values.
4. **The shader uniform contract is preserved exactly.** Every uniform name currently consumed by existing shaders continues to work in v2. New uniforms are additive. See ADR-0005.
5. **Harvest discipline.** Legacy code may contribute *values* (uniform name strings, tuned constants, beat profile presets, sample-format conversion math) and *small pure helpers* (24-bit PCM unpacking, channel layout detection bitmasks). It does **not** contribute whole classes, capture-thread loops, manager implementations, or file structure. The rule is: if you'd quote it in a paper, harvest it; if you'd inherit from it, rebuild it.

## Consequences

### Positive

- **Architecture matches the problem.** The five-layer pipeline (Source → Ring → DSP → Snapshot → Consumers) makes capture, analysis, and presentation independent in a way that v1's shape actively prevents.
- **Extension points are first-class.** New audio sources, new DSP stages, and new beat detectors plug in via three documented interfaces (ADR-0003) without touching unrelated code.
- **Modern C++20 throughout.** `<concepts>`, `<span>`, `<jthread>`, `<stop_token>` yield more compact and less footgun-prone code than the v1 baseline.
- **Headlessly testable** via a `FileSource` reading WAV input plus a `SignalGeneratorSource` for synthetic deterministic frames. CI can run the actual DSP pipeline (ADR-0006).
- **Single source of truth for configuration.** `Setting<T>` declarations are the validator, the UI bound, and the persistence schema simultaneously. The drift class of bug from v1 (`logStrength`) becomes structurally impossible (ADR-0004).
- **~35% smaller.** Estimated 3,500 LOC for v2 versus 5,500 for v1, despite materially expanded capability.

### Negative

- **6-day implementation timeline** from start to first beta DLL (ADR-0007 sequencing).
- **Existing user configs reset.** Users running the v1 to v2 transition lose their tuning. Mitigation: defaults are tuned to be reasonable; beta period absorbs feedback.
- **Risk of perceptual drift.** v2 uses better-justified algorithms (ADR-0007), which may feel different from v1 even when mathematically more correct. Mitigation: no golden-reference parity check (we are *allowed* to surpass v1; ADR-0006), beta period and shader-author feedback drive tuning.
- **Two trees coexist briefly.** `src/` and `src.harvest/` both present until v2 stabilizes. CMake builds only `src/`; `src.harvest/` is documentation. Removed in a final cleanup commit when v2 ships.

### Neutral

- **Public API contract narrows to one frontier.** Shader uniform source strings are stable; everything else is allowed to change. The `extern "C" SwitchAudioProvider` export is replaced by the v2 lifecycle API. The function may continue to exist as a wrapper for backwards compatibility if external callers exist.
- **The repository grows briefly** during the harvest-coexistence period. Not a long-term concern.

## Alternatives considered

### A. In-place refactor of v1

Iteratively extract files, fix the configuration drift, decompose the god-procedure, retire the god-singleton. **Rejected.** The shape of the legacy code fights the architecture we want. Every recent fix attempt has either introduced a new bug (the Phase 0 brace fix didn't fully address the SIMD freeze the user saw), required edits across 4 or more files for what should be a single-layer change (provider switching), or reintroduced concerns we'd already separated (the `ApplyConfigToLiveSystems` path). At ~5,500 LOC the refactor cost approximates the rebuild cost; the rebuild produces a better artifact.

### B. Harvest-and-rebuild with golden-output regression harness

Build a `RecordingTap` against the live v1, capture audio frames and `AnalysisData` snapshots to disk, then verify that the v2 pipeline reproduces those snapshots within ε. **Rejected.** The constraint "match v1 within ε" defeats the point of the rebuild. v2 is intentionally meant to use better algorithms: autocorrelation tempo tracker rather than naive interval, K-weighted loudness rather than simple RMS, per-band onset detection internally. Forcing parity makes those upgrades into "feel different from before" instead of "feel better than before". The honest framing is: this is a beta, behavior is allowed to improve.

### C. Continue patching v1 indefinitely

Accept that the bug-fix-treadmill is the cost of doing business; ship fixes one at a time. **Rejected.** Bug rate is constant under continued patching; each fix has a real risk of introducing the next (we've seen this twice). The clean-room is approximately the same engineering cost as 6 to 12 months of incremental patching, and the result is qualitatively better.

### D. Domain-Driven Design (entities, aggregates, repositories)

**Rejected on its merits.** This is not a domain-rich application. There is no business policy to encode, no transactional state, no entities with identity over time, no consistency boundaries between concepts. The right model is data-flow / SoC, not DDD.

## Sequencing

ADR-0007 contains the day-by-day build plan.

## References

- [research-notes.md](research-notes.md): consolidated research findings feeding the ADRs.
- v1 commits demonstrating the structural defects:
  - `7cda8e8 refactor(phase0): clean dead code, swap to nlohmann/json, fix SIMD brace bug`
  - `1276447 refactor(phase1): introduce AudioPipeline; retire ThreadSafetyManager and global graph`
  - `9a32fcc test(phase3): add GoogleTest target with 19 unit tests covering audit-found bugs`
