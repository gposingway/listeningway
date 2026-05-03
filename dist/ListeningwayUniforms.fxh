// ListeningwayUniforms.fxh.template
// Auto-generated into ListeningwayUniforms.fxh by build.bat.
//
// Listeningway exposes real-time audio analysis to ReShade shaders as
// annotation-bound uniforms. Include this header from your .fx file:
//
//     #include "ListeningwayUniforms.fxh"
//
// LISTENINGWAY_NUM_BANDS is substituted at build time from the addon's
// DEFAULT_NUM_BANDS constant. The runtime band count (Listeningway_NumBands)
// can be lower; iterate only the first int(Listeningway_NumBands) entries
// of any *_FreqBands array — the tail is undefined.
//
// Stability classification per uniform is documented in STABILITY.md.

#define LISTENINGWAY_NUM_BANDS 64
#define LISTENINGWAY_VOLUME_HISTORY_FRAMES 64
// 32 frames keeps NUM_BANDS × frames × 4 bytes within the D3D11 constant
// buffer cap (max 4096 float4 slots) at the default 64 bands, with room
// to spare for the rest of the Listeningway uniforms and any user
// additions. ~0.5 s of history at 60 fps; ~1 s at 30 fps.
#define LISTENINGWAY_FREQBANDS_HISTORY_FRAMES 32
#define LISTENINGWAY_INSTALLED 1

// =========================================================================
// Stable — preserved verbatim from v1
// =========================================================================

uniform float Listeningway_Volume       < source = "listeningway_volume"; >;
uniform float Listeningway_VolumeLeft   < source = "listeningway_volumeleft"; >;
uniform float Listeningway_VolumeRight  < source = "listeningway_volumeright"; >;
uniform float Listeningway_AudioPan     < source = "listeningway_audiopan"; >;
uniform float Listeningway_AudioFormat  < source = "listeningway_audioformat"; >;

uniform float Listeningway_FreqBands[LISTENINGWAY_NUM_BANDS]
    < source = "listeningway_freqbands"; >;

// Live band count Listeningway is currently publishing. Cast to int and
// use as the upper bound when iterating Listeningway_FreqBands.
uniform float Listeningway_NumBands
    < source = "listeningway_numbands"; > = float(LISTENINGWAY_NUM_BANDS);

uniform float Listeningway_Beat         < source = "listeningway_beat"; >;

uniform float Listeningway_TimeSeconds       < source = "listeningway_timeseconds"; >;
uniform float Listeningway_TimePhase60Hz     < source = "listeningway_timephase60hz"; >;
uniform float Listeningway_TimePhase120Hz    < source = "listeningway_timephase120hz"; >;
uniform float Listeningway_TotalPhases60Hz   < source = "listeningway_totalphases60hz"; >;
uniform float Listeningway_TotalPhases120Hz  < source = "listeningway_totalphases120hz"; >;

// 8-bucket directional rose: [Front, FR, Right, BR, Back, BL, Left, FL]
uniform float Listeningway_Direction8[8] < source = "listeningway_direction8"; >;
uniform float Listeningway_Front       < source = "listeningway_front"; >;
uniform float Listeningway_FrontRight  < source = "listeningway_front_right"; >;
uniform float Listeningway_Right       < source = "listeningway_right"; >;
uniform float Listeningway_BackRight   < source = "listeningway_back_right"; >;
uniform float Listeningway_Back        < source = "listeningway_back"; >;
uniform float Listeningway_BackLeft    < source = "listeningway_back_left"; >;
uniform float Listeningway_Left        < source = "listeningway_left"; >;
uniform float Listeningway_FrontLeft   < source = "listeningway_front_left"; >;

// =========================================================================
// Stable — new in v2 (AGC pattern, pre-binned bands, spectral centroid)
// =========================================================================

// AGC-normalized energy: 1.0 = recent average, >1 = louder than average,
// <1 = quieter. Clamped at agc.clamp_max (default 4.0). The "_att" siblings
// are the same value with an asymmetric attack/release smoother applied.
uniform float Listeningway_VolumeNorm < source = "listeningway_volume_norm"; >;
uniform float Listeningway_VolumeAtt  < source = "listeningway_volume_att";  >;
uniform float Listeningway_BassNorm   < source = "listeningway_bass_norm";   >;
uniform float Listeningway_MidNorm    < source = "listeningway_mid_norm";    >;
uniform float Listeningway_TrebNorm   < source = "listeningway_treb_norm";   >;
uniform float Listeningway_BassAtt    < source = "listeningway_bass_att";    >;
uniform float Listeningway_MidAtt     < source = "listeningway_mid_att";     >;
uniform float Listeningway_TrebAtt    < source = "listeningway_treb_att";    >;

// Pre-binned reductions of Listeningway_FreqBands. Average over the
// LISTENINGWAY_NUM_BANDS native bins down to 16 / 32 buckets — convenient
// when your shader wants a fixed-size spectrum regardless of the runtime
// band count.
uniform float Listeningway_FreqBands16[16] < source = "listeningway_freqbands16"; >;
uniform float Listeningway_FreqBands32[32] < source = "listeningway_freqbands32"; >;

// Magnitude-weighted spectral centroid normalized to Nyquist. Useful as a
// "brightness" / color-temperature input.
uniform float Listeningway_SpectralCentroid < source = "listeningway_spectral_centroid"; >;

// =========================================================================
// Experimental — name fixed, semantics may evolve through the v2 beta.
// See STABILITY.md.
// =========================================================================

// K-weighted (BS.1770) momentary loudness, linear (sqrt of mean-square).
uniform float Listeningway_Loudness        < source = "listeningway_loudness"; >;

// Tempo + PLL phase. Phase falls back to 0 when tempo isn't locked; gate
// on tempo_confidence (>= 0.4 = locked).
uniform float Listeningway_BeatPhase       < source = "listeningway_beat_phase"; >;
uniform float Listeningway_TempoBpm        < source = "listeningway_tempo_bpm"; >;
uniform float Listeningway_TempoConfidence < source = "listeningway_tempo_confidence"; >;

// Chronotensity phases — energy-accumulator alternatives to BeatPhase.
// Robust replacements when the tempo estimator isn't locked.
uniform float Listeningway_PhaseVolume     < source = "listeningway_phase_volume"; >;
uniform float Listeningway_PhaseBass       < source = "listeningway_phase_bass"; >;
uniform float Listeningway_PhaseTreble     < source = "listeningway_phase_treble"; >;

// Trail / waterfall sources. Layout:
//   VolumeHistory: oldest at index 0, newest at index VOLUME_HISTORY_FRAMES-1.
//   FreqBandsHistory: band-major, time-ascending. Index helper:
//       freqbands_history[band * LISTENINGWAY_FREQBANDS_HISTORY_FRAMES + frame]
//   frame=0 is oldest. Active band axis = NumBands.
uniform float Listeningway_VolumeHistory[LISTENINGWAY_VOLUME_HISTORY_FRAMES]
    < source = "listeningway_volume_history"; >;
uniform float Listeningway_FreqBandsHistory
    [LISTENINGWAY_NUM_BANDS * LISTENINGWAY_FREQBANDS_HISTORY_FRAMES]
    < source = "listeningway_freqbands_history"; >;
