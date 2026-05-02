# Architecture Decision Records

This directory holds the architectural decisions that govern the Listeningway v2 engine rebuild. Each ADR captures a single decision point: the context that forced the decision, the choice made, the consequences, and the alternatives considered.

ADRs are immutable once accepted. Superseding decisions get a new ADR that references the old one rather than editing in place.

## Format

ADRs follow [MADR](https://adr.github.io/madr/) structure:

```
# ADR-NNNN: Title

## Status        — Proposed | Accepted | Superseded by ADR-XXXX | Deprecated
## Context       — what's forcing the decision
## Decision      — what we chose
## Consequences  — positive / negative / neutral
## Alternatives  — what we considered and rejected
```

## Index

| # | Title | Status |
|---|---|---|
| [0001](0001-clean-room-v2-engine.md) | Clean-room v2 engine — rebuild rather than refactor | Accepted |
| [0002](0002-pipeline-architecture.md) | Five-layer pipeline: Source → Ring → DSP → Snapshot → Consumers | Accepted |
| [0003](0003-adapter-usage-policy.md) | Adapter pattern usage policy — three interfaces, no more | Accepted |
| [0004](0004-configuration-strategy.md) | Configuration: single Settings struct, declarative bounds, auto-marshalled JSON | Accepted |
| [0005](0005-uniform-contract.md) | Shader uniform contract — preserve names, expand additively | Accepted |
| [0006](0006-testing-strategy.md) | Testing strategy — property-based, FileSource replay, no parity baseline | Accepted |
| [0007](0007-v1-scope.md) | v1 scope lock — what's in, what's deferred | Accepted |
| [0008](0008-language-and-dependencies.md) | Language and dependencies — C++20, vcpkg-pinned libraries | Accepted |
| [0009](0009-process-audio-source.md) | ProcessAudioSource — per-process loopback for game-only isolation | Accepted |
| [0010](0010-network-outputs.md) | Network outputs and the `IOutputConsumer` abstraction | Accepted |
| [0011](0011-osc-output.md) | OSC output consumer — tinyosc embed + address schema | Accepted |
| [0012](0012-openrgb-output.md) | OpenRGB output consumer — cppSDK embed + opinionated mapping | Accepted |

## Reading order for new contributors

If you're new to the project, read in numerical order. ADR-0001 sets the strategic context; ADR-0002 sets the structural foundation; ADRs 0003–0006 are the design rules that follow from it; ADR-0007 is the build plan; ADR-0008 is the toolchain.
