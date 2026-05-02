# audio

Audio-related code: capture, analysis, beat detection, and providers.

## Structure
- `capture/`: provider interfaces and implementations
	- `providers/` includes `audio_capture_provider_system.*` (WASAPI loopback) and `audio_capture_provider_off.*` (None/Off)
	- `audio_capture_manager.*`: registry, default selection by `is_default`, switching, and lifecycle
- `analysis/`: FFT engine, analyzers, caches, tempo worker
	- SSE SIMD hot paths with scalar fallback; runtime toggle via `audio.simdEnabled`

## Data flow
WASAPI capture → SPSC ring buffer → Analyzer (FFT + bands + beat) → Uniform updates.

## Notes
- Switching to Off clears `AudioAnalysisData` immediately; switching back performs a cold start if needed.
- Default provider is System Audio (marked `is_default`).
- Hann window and band mapping are cached; FFT buffers are reused.

## Troubleshooting
- If switching providers seems unresponsive, check `listeningway.log` and ensure debug logging is enabled in the overlay.
