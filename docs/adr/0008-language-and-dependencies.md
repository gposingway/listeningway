# ADR-0008: Language and dependencies

## Status

Accepted — 2026-05-01

## Context

v1 targets C++17. v2 commits to a clean-room rebuild (ADR-0001), which is a natural moment to revisit language version, library dependencies, and the build/test toolchain. ADR-0001's beta-release scope removes any compatibility constraint on tooling — we can pick what's right for v2 now.

The platform constraints are:
- **Windows-only.** ReShade is Windows-focused; cross-platform doesn't pay.
- **MSVC 2022.** ReShade SDK headers are MSVC-tested; switching compiler suite would multiply build complexity.
- **vcpkg-managed third-party libraries** (already in use; manifest-mode).
- **ReShade SDK v6.3.3** (existing pin in `prepare.bat`).

## Decision

### Language: C++20

v2 targets **C++20** with MSVC 2022 (`/std:c++20`). v1's `set(CMAKE_CXX_STANDARD 17)` is bumped.

Specific C++20 features v2 uses meaningfully:

| Feature | Where used | Why it earns its keep |
|---|---|---|
| `<concepts>` and concepts/requires clauses | `IDspStage` template helpers; `Setting<T>` constraints; test generators | Replaces SFINAE noise; compile errors are readable |
| `<span>` (`std::span<const float>`) | Audio sample interfaces; `IAudioSource::FrameSink`, `IDspStage::process` inputs | No-allocation pass-through of contiguous arrays; replaces "raw pointer + size" pairs |
| `<bit>` (`std::bit_ceil`, `std::countl_zero`) | FFT size validation; ring buffer capacity | Power-of-two helpers without bit-twiddling tricks |
| `<jthread>` and `std::stop_token` | Capture thread, DSP thread, worker thread | Cooperative cancellation replaces atomic-bool soup; threads self-join on destruction |
| `<chrono>` calendar / duration arithmetic | Profiling, AGC time windows, stale detection | Type-safe time math throughout |
| Designated initializers (`AudioSnapshot{ .volume = ..., .beat = ... }`) | Snapshot construction in tests | Self-documenting test setup |
| Three-way comparison (`<=>`) where useful | Value-type comparisons in tests | Less boilerplate |
| `consteval` | Compile-time channel-layout tables, format constants | Zero-runtime-cost lookups |

C++20 modules are **not** used for v2 — MSVC support is acceptable but not yet smooth across vcpkg-built dependencies. Reconsider for a later v2.x.

C++23 features (e.g. `std::expected`, `std::print`) are **not** used; MSVC support is partial as of mid-2026 and the gain over C++20 is marginal.

### Compile flags

```
/std:c++20
/W4                    # high warning level
/WX                    # warnings as errors (CI; relaxed for local dev)
/permissive-           # strict conformance
/Zc:preprocessor       # standards-compliant preprocessor
/Zc:__cplusplus        # actual __cplusplus value
/EHsc                  # standard exception handling
/MP                    # multi-processor compile
/Zc:throwingNew        # operator new throws on failure
```

Optimization flags per configuration follow MSVC defaults; `/O2 /Oi /GL` for Release.

### Dependencies (vcpkg manifest)

```jsonc
{
  "name": "listeningway",
  "version-string": "2.0.0-beta.1",
  "dependencies": [
    "kissfft",
    "nlohmann-json",
    "gtest",
    "rapidcheck",
    "readerwriterqueue"
  ]
}
```

| Library | Version target | Justification |
|---|---|---|
| `kissfft` | latest stable | FFT engine. Already in v1; well-tested; no reason to switch. |
| `nlohmann-json` | ≥ 3.11 | JSON marshalling. Used for the auto-generated `Settings` round-trip (ADR-0004) via `NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT`. Already in v1 (since Phase 0). |
| `gtest` | ≥ 1.14 | Unit + replay test framework (ADR-0006). Already in v1 (since Phase 3). |
| `rapidcheck` | latest stable | Property-based test framework (ADR-0006). New for v2. |
| `readerwriterqueue` | latest stable | Cameron Desrochers' moodycamel header-only SPSC queue. New for v2; replaces hand-rolled SPSC ring per ADR-0002 / [research-notes.md §6](research-notes.md). BSD-2 license. |

### Triplet

`x64-windows-static` (preserved from v1). Static linkage avoids a vcpkg DLL dependency tree shipping alongside the addon.

### CMake

Minimum CMake `3.21` (was `3.15` in v1). The bump enables:
- `target_sources(... FILE_SET HEADERS ...)` for clean public/private header separation.
- `cmake_policy(SET CMP0091 NEW)` runtime library default propagation (already set in v1).
- `find_package` requirement consistency improvements.

Project structure:

```
CMakeLists.txt                  # top-level
src/
  CMakeLists.txt                # v2 engine library (the addon DLL)
  audio/
    source/                     # IAudioSource implementations
    ring/                       # FrameRing wrapper around moodycamel
    dsp/                        # IDspStage implementations
    snapshot/                   # SeqlockSnapshot, AudioSnapshot type
    pipeline/                   # Pipeline class, AudioSystem orchestrator
  config/                       # Setting<T>, Settings, Store
  output/                       # shader_contract.h, UniformPublisher
  overlay/                      # ImGui panels
  third_party/                  # vendored single-header libs (readerwriterqueue.h, rapidcheck — if not via vcpkg)
tests/
  CMakeLists.txt
  generators.h
  data/                         # WAV files for replay tests
  stages/ sources/ snapshot/ config/ replay/ integration/
src.harvest/                    # legacy v1 (read-only reference; not in build)
```

`src.harvest/` is excluded from the CMake glob explicitly; `add_subdirectory(src)` only.

### Test runtime configuration

Per Phase 3 v1 work, the test executable opts into `/MT` to match `gtest` from the static-runtime triplet, while the main DLL keeps `/MD` (default). `CMP0091 NEW` policy is already set; v2 keeps it.

## Consequences

### Positive

- **Modern, readable code.** Concepts replace SFINAE; spans replace pointer+size; jthread replaces manual join discipline.
- **Property tests via rapidcheck.** Header-only, BSD, integrates with gtest. Single dependency for the testing strategy in ADR-0006.
- **moodycamel SPSC** is one of the most stress-tested lock-free queues in C++. Risk reduction vs hand-rolled.
- **vcpkg manifest stays slim** (5 libraries). Each one earns its keep.
- **One static-runtime triplet** for everything; consistent linkage.

### Negative

- **C++20 raises the MSVC version floor.** MSVC 2022 17.x is widely available; existing developer setups likely already have it. Documented in `prepare.bat` if needed.
- **Two new vcpkg dependencies** (`rapidcheck`, `readerwriterqueue`) for v2. Both are header-only, fast to build. Negligible CI impact.
- **C++20 feature usage requires reviewer familiarity.** Concepts + jthread + stop_token are not yet universal C++ knowledge. Mitigation: PR reviewers walk through them; documentation comments where idiomatic style is non-obvious.

### Neutral

- **Build time** — C++20 compilation is slightly slower than C++17 due to concepts and modules-related machinery (even when modules aren't used). Negligible for a project this size.
- **Static analysis** — clang-tidy / cppcheck both support C++20 sufficiently for our needs.

## Alternatives considered

### Stay on C++17
**Rejected.** ADR-0001's "while we're here" mandate; C++20 readability and safety wins are concrete (concepts, jthread, span) and widely adopted in production C++ code by 2026.

### C++23
**Rejected for v1.** MSVC 2022's C++23 support is partial; some `<expected>` and `<print>` corners are still maturing. v2.x can reconsider.

### Replace kissfft with FFTW
**Rejected.** FFTW is GPL (or commercial license fee). kissfft is BSD; sufficient for our FFT sizes. No compelling reason to switch.

### Replace kissfft with Intel IPP
**Rejected.** Closed-source, distribution-restricted. kissfft is sufficient.

### Replace nlohmann-json with simdjson
**Rejected.** simdjson optimizes parsing speed; we don't have parsing-speed pressure. nlohmann's intrusive macros are the feature we want; simdjson lacks them.

### Add `xsimd` for portable SIMD
**Deferred to v1.5.** ADR-0007 covers; not in v1. Hand-written SSE3 in the FFT magnitude path is acceptable.

### Add `fmt` for formatting
**Rejected.** C++20 `<format>` is sufficient for v2's needs. MSVC's implementation is conformant.

### Add a logging library (`spdlog`, `quill`)
**Rejected.** v1's hand-written `LogToFile` works; logging volume is low (debug-only paths). A library would be more code, not less.

### Add a CLI argument parser for `listeningway_replay` test driver
**Deferred.** Replay tests in v1 are gtest cases; no separate driver binary. v1.5 if a standalone replay CLI is wanted.

## References

- ADR-0001 — beta scope; toolchain choice freedom.
- ADR-0002 — pipeline architecture; `moodycamel::ReaderWriterQueue` per [research-notes.md §6](research-notes.md).
- ADR-0004 — `nlohmann-json` intrusive marshalling.
- ADR-0006 — `gtest` + `rapidcheck` for testing.
- ADR-0007 — v1 scope; detection-technique implementation cites kissfft.
