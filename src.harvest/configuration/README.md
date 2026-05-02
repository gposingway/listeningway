# configuration

Configuration management: load/save `Listeningway.json`, validate, and live-apply to systems.

Key fields:
- `audio.captureProviderCode`: "system" or "off"; default chosen by provider `is_default`.
- `audio.analysisEnabled`: toggled automatically based on provider selection.
- `audio.simdEnabled`: enable SSE-accelerated analysis; scalar fallback when false.

Manager responsibilities:
- Validate provider code against available providers; prefer `is_default`.
- Apply changes at runtime (stop/start capture, clear data on Off).
