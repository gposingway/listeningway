# OpenRGB-cppSDK — vendored OpenRGB protocol client

**Upstream**: https://github.com/Youda008/OpenRGB-cppSDK
**Author**: Jan Broz (`Youda008`)
**License**: MIT (see [`LICENSE`](LICENSE))
**Imported at commit**: 2026-05-02 from `Youda008/OpenRGB-cppSDK` master,
with submodules resolved (`external/CppUtils-Essential`,
`external/CppUtils-Network`).

## What we use

The full library, hooked via `add_subdirectory()` from our top-level
`CMakeLists.txt`. The `orgbsdk` static target is linked into the
Listeningway addon DLL by [`src/output/openrgb_consumer.cpp`](../../src/output/openrgb_consumer.cpp).

The CMake graph also picks up:

- `external/CppUtils-Essential/` — Youda008's stdlib-style helpers
  (BinaryStream, Endianity, Span, ContainerUtils, ...).
- `external/CppUtils-Network/` — TCP socket + WSAStartup wrapper, used
  by `orgbsdk`'s `Client::connect()`.

The `tools/orgbcli/` directory (an interactive command-line OpenRGB
client) is included by upstream with `EXCLUDE_FROM_ALL` and never
builds in our pipeline.

## Why this library

Picked for the v2.x OpenRGB consumer (ADR-0012). Pre-2026 industry
research already validated it ([`research-notes-process-audio.md`](../../docs/adr/research-notes-process-audio.md)
and the user-wishlist findings):
the only serious C++ client, MIT-licensed, stdlib-only deps, handles
the protocol-version negotiation we explicitly don't want to redo
ourselves (mishandled it can corrupt server-side memory per the
upstream reference). Five public headers; the rest is internal.

## Update procedure

```
git -C ../../../_listeningway-research/OpenRGB-cppSDK pull
git -C ../../../_listeningway-research/OpenRGB-cppSDK submodule update --init --recursive
rm -rf "third_party/Youda008-OpenRGB-cppSDK"
cp -r ../../../_listeningway-research/OpenRGB-cppSDK third_party/Youda008-OpenRGB-cppSDK
find third_party/Youda008-OpenRGB-cppSDK -name ".git" -type d -prune -exec rm -rf {} +
find third_party/Youda008-OpenRGB-cppSDK -name ".gitmodules" -delete
rm -f third_party/Youda008-OpenRGB-cppSDK/.gitignore third_party/Youda008-OpenRGB-cppSDK/.clang-format
```

Re-add this `ATTRIBUTION.md`. Re-run `build.bat` and verify the build
is green. If upstream changed the `Client` API, update
[`src/output/openrgb_consumer.cpp`](../../src/output/openrgb_consumer.cpp).

## Local modifications

None. The vendored copy is byte-identical to the upstream tag (modulo
removed `.git`-related metadata).
