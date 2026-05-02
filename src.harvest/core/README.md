# core

Core logic and cross-cutting concerns: overlay, configuration, thread safety, and addon entrypoints.

## Overlay
- Provider dropdown remains interactive during switching and binds to `audio.captureProviderCode`.
- SIMD toggle available; falls back to scalar if disabled.

## Configuration
- `configuration_manager.*` owns `Listeningway.json` persistence and applies live changes.
- Default provider code is validated against available providers; prefers `is_default`.

## Logging
- Thread-safe file logging to `listeningway.log` next to settings.
- Enable debug logging in overlay to see detailed events.
