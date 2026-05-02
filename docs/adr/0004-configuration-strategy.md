# ADR-0004: Configuration. Single Settings struct, declarative bounds, auto-marshalled JSON

## Status

Accepted, 2026-05-01

## Context

v1 has the same setting declared in three places that drift from each other:

1. **The default value**. Declared in `src/core/constants.h` as a `constexpr float DEFAULT_*`.
2. **The validator clamp range**. Declared in `Configuration::Validate()` as a magic-number `std::clamp(field, lo, hi)` call.
3. **The UI slider range**. Declared in `src/core/constants.h` as a `constexpr float OVERLAY_*_MIN/MAX`.

The recent `frequency.logStrength` bug was the canonical example: default `0.1`, validator range `[0.2, 3.0]`, UI slider range `[0.01, 1.5]`. Save the default, load the file, validator clamps `0.1` to `0.2`. Value lost. Three independent declarations, three opportunities to drift.

In addition, JSON marshalling is hand-written: ~100 lines of `j["frequency"] = { {"logStrength", frequency.logStrength}, ... }` followed by ~80 lines of `load_if(f, "logStrength", ...)`. Adding a field requires touching both halves *and* the validator *and* the UI; forgetting any one is a silent bug.

The visualizer-design research (see [research-notes.md](research-notes.md) §4) also surfaces that v2 needs *more* settings (AGC time constants, chronotensity rates, loudness window length, multiple band-count exposures), so the v1 maintenance pattern would scale poorly.

## Decision

### Single declaration per setting via `Setting<T>`

Each tunable is declared exactly once as a `Setting<T>` value with its default, bounds, JSON key, and tooltip. UI binding, persistence, and validation all read from the same declaration.

```cpp
template <typename T>
struct Setting {
    T default_value;
    T min;
    T max;
    std::string_view key;        // JSON key + UI label key
    std::string_view tooltip;    // shown in overlay tooltip

    T clamp(T candidate) const { return std::clamp(candidate, min, max); }
};
```

(Specializations or extensions for non-numeric settings. E.g. `Setting<std::string>` for the source code, `Setting<bool>`. Follow the same pattern with appropriate validation.)

### Single `Settings` struct as the persisted shape

All settings live in one POD-like `Settings` struct, organized into nested sub-structs by concern (`audio`, `beat`, `frequency`, `debug`, etc.). The struct is the in-memory representation, the persisted shape, and the parameter pass-through into the DSP pipeline.

### Auto-marshalled JSON via `nlohmann::json` intrusive macros

```cpp
struct FrequencyConfig {
    bool   logScaleEnabled = true;
    float  logStrength     = 0.1f;
    float  minFreq         = 183.0f;
    float  maxFreq         = 22050.0f;
    // ...
};
NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(FrequencyConfig,
    logScaleEnabled, logStrength, minFreq, maxFreq /*...*/);
```

Adding a new field is one line in the struct + one entry in the macro. JSON keys default to field names (we override only when historical naming requires it). Missing fields on load fall through to the default value.

### Atomic version counter for hot reload

```cpp
class Store {
public:
    Settings load(const std::filesystem::path&);
    bool     save(const Settings&, const std::filesystem::path&) const;

    uint64_t version() const noexcept;        // bumped on each publish
    void     publish(Settings new_settings);  // setter; bumps version
    Settings snapshot() const;                // thread-safe copy
};
```

Each DSP stage caches its own `Settings` snapshot and the version counter it was taken at. On each frame, the stage compares the current version to its cached version (atomic relaxed load); if different, refetches and rebuilds any derived caches (band edges, equalizer LUT, etc.). This avoids a per-frame mutex on the configuration store.

### Validation runs on load and on UI write

`Setting<T>::clamp(candidate)` is called:
1. After deserialization from JSON (load path).
2. When the overlay writes a new value (UI path).
3. Never on the read path. The DSP thread trusts that whatever's in the live `Settings` snapshot is already validated.

This eliminates the v1 drift class because a single `Setting<T>::clamp` call is the *only* validator anywhere; UI sliders read `Setting<T>::min`/`max` for their range; persistence reads `Setting<T>::default_value` for missing fields.

### v1 ships without a migration framework

ADR-0001 commits to a beta release with no behavior-parity guarantee against v1. Likewise, the v1 → v2 configuration file is not migrated. Old `Listeningway.json` files are ignored; defaults regenerate on first run.

A `schema_version` field is included in v2's JSON from day one (currently `1`). It costs zero today and gives us a migration anchor for the v2 → v3 transition (or any future schema break).

### Fields not persisted

The `Settings` struct holds only persistent configuration. Transient runtime state lives elsewhere:

| Held in `Settings` | Held in `AudioSnapshot` (transient) | Held in source/stage state (transient) |
|---|---|---|
| User-set tunables: amplifiers, EQ, beat thresholds, AGC window, beat profile, debug flags, source code | Computed analysis: volume, bands, beat, pan, direction8, tempo, snapshots history | Source-detected sample rate; current capture format; FFT cache; band-edge cache |

Sample rate is detected at capture time and lives on the `AnalysisFrame` (via `Format`). It is never written to the config file. The config file should be portable across audio devices.

## Consequences

### Positive

- **Drift class eliminated.** The v1 logStrength bug is structurally impossible: there is one `min`, one `max`, one `default_value` per setting.
- **~150 LOC of marshalling deleted.** v1's hand-written `Save()` and `LoadFromJson()` collapse into the `nlohmann` macro line per struct.
- **Adding a setting is one line in the struct + one entry in the marshalling macro + one `Setting<T>` declaration.** No ceremony.
- **Hot reload without a hot-path mutex.** DSP stages cache their settings snapshot; an atomic version compare gates the refetch.
- **UI auto-generation possible.** Future improvement: walk the `Settings` struct via reflection (or a manual table) to render an overlay panel automatically. Out of scope for v1 but the design supports it.
- **Validation is centralized and testable.** Unit tests pin `Setting<T>::clamp` behavior per field; round-trip property tests (load → validate → save → load → validate yields the same value) become trivial.

### Negative

- **`Settings` struct is large.** Probably 30–50 fields across nested sub-structs. Acceptable; it reflects the genuine complexity of the configurable surface.
- **`NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT` requires the type to be default-constructible** (it is).
- **No migration framework in v1** means existing v1 user configs are silently dropped. Beta acceptance covers this; documented in ADR-0001.

### Neutral

- **Schema version is permanent.** Once we ship `schema_version = 1`, all future schema changes go through migration code. This is the right discipline.

## Alternatives considered

### Continue with hand-written Save / Load
**Rejected.** Source of the drift bugs. Each new field is two places to change correctly, and "did you remember the load side?" is a perpetual review burden.

### Reflection-based auto-marshalling (e.g. `boost::pfr`)
**Rejected for v1.** `nlohmann`'s intrusive macros are sufficient and explicit. Field renames are visible in the macro list; reflection-based marshalling makes them invisible. Reconsider if `Settings` grows large enough that the macro list becomes unwieldy.

### Per-field separate JSON files (one file per concern)
**Rejected.** Splits a small file (~3 KB JSON) into many smaller files. No win; complicates atomic save.

### YAML or TOML instead of JSON
**Rejected.** JSON is already in use (v1), `nlohmann::json` is already a dependency, and shader authors / users are familiar with it. No reason to change format for v2.

### Custom validator types per setting (e.g. `Setting<float, MinMaxClamp>`)
**Rejected for v1.** Most settings are numeric `[min, max]` clamps; handling the edge cases (string enums, paths) inline in the struct is fine. Reconsider if validation logic grows beyond clamps.

### `Settings` as immutable, with copy-on-write for changes
**Adopted, partially.** The DSP read path treats `Settings` as immutable (cached snapshot). The UI write path replaces the live `Settings` via `Store::publish(new_settings)`, which is conceptually copy-on-write. Implementation: `Store` holds the canonical `Settings` and a `version_` atomic. `publish` takes a write lock briefly; readers take a read snapshot under the same lock. Can be optimized later (e.g. seqlock the settings too) but not needed for v1.

## References

- v1 bug demonstrating the drift: `frequency.logStrength` reset on save+load (`src/configuration/Configuration.cpp` line 51 in commit 7cda8e8).
- [research-notes.md §3](research-notes.md). JUCE `AudioProcessorValueTreeState` validates the "atomic snapshot read on hot path" pattern.
- [research-notes.md §4](research-notes.md). Visualizer research that motivates additional settings.
- ADR-0005. Uniform contract; configuration changes ripple to which uniforms are populated but uniform names themselves are stable.
