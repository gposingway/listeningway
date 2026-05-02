# tinyosc — vendored OSC encoder

**Upstream**: https://github.com/mhroth/tinyosc
**Author**: Martin Roth (`mhroth`)
**License**: ISC (see [`LICENSE`](LICENSE))
**Imported at commit**: 2025-05-02 from `mhroth/tinyosc` master (depth-1 clone)

## What we use

- `tinyosc.c` — OSC message encoder/decoder (we use the encoder side).
- `tinyosc.h` — public API header.

The `main.c` example shipped upstream is **not** vendored — it depends on
POSIX sockets and is not relevant to our integration. We bring our own
Winsock socket plumbing in [`src/output/osc_consumer.cpp`](../../src/output/osc_consumer.cpp).

## Why this library

Picked for the v2.x OSC consumer (ADR-0011). Two-file ISC-licensed C
library, already MSVC-aware (`_WIN32` paths use `winsock2.h` / `htonl` /
`strncpy_s` with `_TRUNCATE`), stateless writer that drops into a
caller-owned buffer, no transport coupled in. Smallest credible
dependency footprint for our send-only OSC use case.

## Update procedure

To pick up upstream changes:

```
git -C ../../../_listeningway-research/tinyosc pull
cp ../../../_listeningway-research/tinyosc/tinyosc.{c,h} \
   ../../../_listeningway-research/tinyosc/LICENSE .
```

Re-run `build.bat` and verify the build is green. If upstream introduced
new functions that conflict with our wrapper, update
[`src/output/osc_consumer.cpp`](../../src/output/osc_consumer.cpp).
