# Research notes: clean-room beat tracker for the BeatStage rewrite

Consolidated study of two reference implementations of real-time beat tracking — Adam Stark's BTrack (GPLv3, Queen Mary University of London) and Paul Brossier's aubio (BSD-3) — read end-to-end with the goal of writing a clean-room replacement of `BeatStage`. Source citations point at the local checkouts in `_listeningway-research/BTrack` and `_listeningway-research/aubio`. The existing BeatStage uses naive autocorrelation with a log-Gaussian prior at 120 BPM and a `1 - second_best/best` confidence that reads ~20% even on metronomic dance music; both reference designs avoid that failure mode and we want to understand exactly how before rewriting.

---

## 1. BTrack algorithm walkthrough

BTrack is the C++ realisation of Stark's PhD thesis, "Automatic Real-Time Beat Tracking" (Queen Mary, 2011). The pipeline is four stages: ODF computation, ODF buffering, tempo estimation by comb filterbank with tempo state-transition smoothing, and beat-phase prediction by cumulative-score search.

### a. Onset detection function

The default ODF is **complex spectral difference, half-wave rectified** (CSD-HWR), set in all three constructors at `BTrack.cpp:31, 38, 45`. The implementation lives at `OnsetDetectionFunction.cpp:520-564`. For each FFT bin the algorithm computes the *predicted* phase as a linear extrapolation of the previous two phases (`phase[i] - 2*prevPhase[i] + prevPhase2[i]` at line 542) and the magnitude difference (line 545). When the magnitude has *risen* between frames, the bin contributes the Euclidean distance between the actual and predicted complex values: `sqrt(|X|² + |X_prev|² - 2·|X|·|X_prev|·cos(Δϕ))` at line 551. The half-wave rectification (line 548) discards bins that decreased in magnitude, which is what makes this an *onset* detector rather than a generic novelty score — note offsets and tail decays are suppressed.

Alternative ODFs are exposed via the `OnsetDetectionFunctionType` enum (`OnsetDetectionFunction.h:37-49`): `EnergyEnvelope`, `EnergyDifference`, `SpectralDifference` (linear absolute diff), `SpectralDifferenceHWR`, `PhaseDeviation`, `ComplexSpectralDifference`, `ComplexSpectralDifferenceHWR` (default), `HighFrequencyContent`, `HighFrequencySpectralDifference`, and the HWR variant of the last. Bello et al.'s 2005 *Tutorial on Onset Detection in Music Signals* is the canonical reference for which descriptor is best for what material: HFC for percussive content, complex-domain for material with both percussive and pitched content, spectral flux for general music, phase-only for soft tonal onsets.

Default frame and hop sizes are 1024 / 512 samples (`BTrack.cpp:31`), giving ~86 ODF samples/sec at 44.1 kHz — i.e. one ODF sample per audio hop. The window is Hanning. The FFT input is rotated by half a frame before transform (`OnsetDetectionFunction.cpp:269-275`) to centre the window on the hop boundary, which gives each bin a zero-phase response and makes the phase-prediction term in CSD meaningful. There is no smoothing of the ODF itself — each hop produces one raw ODF sample which is later fed through the cumulative-score smoothing inside the beat tracker.

### b. Tempo estimation

The ODF is buffered in a circular buffer sized `(512 * 512) / hopSize` (`BTrack.cpp:153`) — at hop=512, that's 512 samples ≈ 5.9 s of history. Tempo is estimated only when a beat fires (`BTrack.cpp:251`), so updates run at the beat rate (≈2 Hz at 120 BPM), not every hop. The buffer is first **resampled to a fixed length of 512** using libsamplerate's SINC interpolator (`BTrack.cpp:347-370`), so all downstream sizes are constant regardless of host hop size.

Tempo is *not* computed by direct ACF peak-pick. The chain is:

1. **Adaptive threshold** the resampled ODF (`BTrack.cpp:442-480`): subtract a sliding-window mean computed over `[i-8, i+7]`, clamp negative values to zero. This is a moving-average detrender, not a median-based one (BTrack and aubio differ here — aubio uses median).
2. **Balanced ACF via FFT** (`BTrack.cpp:501-590`): zero-pad the 512-sample ODF to 1024, FFT, multiply by complex conjugate, IFFT, divide each lag by `(N-lag)` to compensate for the smaller overlap at large lags ("balanced" autocorrelation, line 581).
3. **Shift-invariant comb filterbank** (`BTrack.cpp:483-498`): for each candidate lag `i` in [2, 127], sum the ACF at integer multiples `a·i + b` for `a ∈ [1,4]` (number of harmonics) and `b ∈ [1-a, a-1]` (a `2a-1`-wide neighbourhood around each multiple), normalised by `1/(2a-1)`. The result is weighted by a precomputed Rayleigh distribution with parameter 43 (`BTrack.cpp:88, 102`). The Rayleigh prior peaks at lag 43 ≈ 120 BPM at 44.1 kHz/512, which is the standard musical-tempo prior from Davies & Plumbley 2007. This step jointly handles octave errors (the comb sums energy at lag, 2·lag, 3·lag, 4·lag, so a true tempo of T scores higher than its half/double aliases) *and* applies a perceptual prior toward 120 BPM.
4. **Tempo observation vector** of length 41 (`BTrack.cpp:390-395`): each index `i` represents a tempo from 80 to 160 BPM in 2 BPM steps. The score is the comb-filter output at the corresponding lag *plus* the comb-filter output at the half-tempo lag (line 394). This double sampling makes the tempo posterior more robust at the boundaries of the search range.
5. **Viterbi-style temporal smoothing** with a 41×41 Gaussian transition matrix (`BTrack.cpp:114-122`, applied at `393-417`). For each new tempo candidate `j`, find `max_i prevDelta[i] · transition[i][j]`, multiply by the new observation, store as `delta[j]`, normalise. The argmax of `delta` is the new tempo estimate. The transition matrix has `σ = 41/8 ≈ 5.1` indices ≈ 10 BPM, so the tempo can drift but cannot jump.

Confidence is *not* explicitly reported by BTrack's public API — `getCurrentTempoEstimate()` returns the BPM but there's no `getTempoConfidence()`. The `delta` vector after normalisation is implicitly a posterior probability and could be summarised by, e.g., the ratio of the top peak to the sum of the rest — that's the natural place to add a confidence read-out in our port.

### c. Beat tracking

BTrack uses Davies & Plumbley's *two-state* model (Davies & Plumbley, "Context-Dependent Beat Tracking of Musical Audio", IEEE TASLP 2007). The two states in BTrack's case are:

- **General state**: no prior tempo estimate; uses the Rayleigh-weighted comb filterbank described above.
- **Context-dependent state**: once a stable tempo is found, switch to a Gaussian prior centred on that tempo (narrower than Rayleigh) plus a Gaussian phase prior centred on the predicted next beat.

In BTrack the context-dependent behaviour is implicit in the temporal smoothing of the tempo posterior (the transition matrix biases continuity) and explicit in the beat-phase prediction window (see (d)). The tempo-fixing API at `BTrack.cpp:314-337` is essentially "force context-dependent state with a user-supplied tempo".

The cumulative-score function `C[n]` is the heart of beat tracking. At each ODF sample (`BTrack.cpp:617-632`):
- A search window is opened from `n - 2·beatPeriod` to `n - beatPeriod/2` — i.e. between two beat periods ago and half a beat period ago.
- The cumulative score in that window is weighted by a **log-Gaussian transition window** `W1` (Stark thesis equation 3.2, see `BTrack.cpp:708-720`): `exp(-½·(α·log(-v/β))²)` where `α = tightness = 5` and `β` is the beat period. This window peaks at exactly one beat period ago and falls off in log-time, so doubling the lag costs the same as halving it — symmetric in *musical* time.
- The maximum weighted cumulative score in the window plus `(1-α) · ODF[n]` (with `α = 0.9`, line 92) becomes `C[n]`. This is Stark thesis equation 3.4. The 0.9 mixing means the cumulative score has strong "momentum": each new ODF sample only nudges it 10%.

`C[n]` is *not* an onset signal — it's a continuous beat-likelihood signal. Peaks in `C[n]` align with beats whether or not an ODF onset was detected exactly there.

### d. Beat-phase prediction

This is the part we care about most. BTrack does not say "a beat just fired" — it says "the next beat will fire `timeToNextBeat` ODF samples from now". The prediction routine is `predictBeat()` at `BTrack.cpp:635-705`:

1. **Synthesise future cumulative score**. Build a buffer `futureCumulativeScore` of length `onsetDFBufferSize + beatExpectationWindowSize` (one beat period ahead). Copy the existing `C[n]` history into the first part, then for each future sample, run the same log-Gaussian-weighted maximum *with the new ODF sample set to zero and α set to 1.0* (line 678) — i.e. let the cumulative score's own momentum carry it forward without any new evidence.
2. **Apply a Gaussian beat-expectation window** `W2` (Stark thesis equation 3.6, `BTrack.cpp:652-656`): `exp(-(v - β/2)² / (2·(β/2)²))` over the next beat period. This window peaks at exactly half a beat period ahead — i.e. we expect the next beat near the centre of the prediction window.
3. **Argmax** of `W2 · futureCumulativeScore` over the next beat period gives `timeToNextBeat` (lines 690-701). The next beat is predicted at that offset; the next *prediction* (not the next beat) is scheduled half a beat period after the predicted beat (line 704), so BTrack always re-predicts on the off-beat.

Crucially, predictions are scheduled *between* beats (`BTrack.cpp:241-242`), not at beats. The downstream consumer reads `timeToNextBeat` (a count of remaining ODF samples) and `beatDueInCurrentFrame()` (a one-shot boolean that fires when the counter hits zero). This is exactly the forward-prediction contract we want for the `beat_phase` shader uniform — we always know how far ahead the next beat is, so the visualizer can interpolate continuously instead of decaying from the last detected beat.

---

## 2. aubio algorithm walkthrough

aubio is a C library implementing the same Davies & Plumbley 2004 paper but with different design choices around the periphery — different default ODF, different peak-picker, separate `onset` and `tempo` modules, and a smaller per-call work budget.

### a. ODF

`specdesc.c` implements: `energy` (`L186-198` in our analysis above; sum of squared magnitudes, line 95-97 of file), `hfc` (sum of `(k+1)·|X[k]|`, line 105-107 — same formula as BTrack's `HighFrequencyContent`), `complex` (complex spectral difference, line 112-131; equivalent to BTrack's `ComplexSpectralDifference` *without* HWR — aubio doesn't half-wave-rectify the complex variant), `phase` (phase deviation through a histogram, line 135-162), `wphase` (phase weighted by magnitude, line 165-179), `specdiff` (Euclidean magnitude difference followed by histogram, line 182-205), `kl` and `mkl` (Kullback-Leibler and modified KL of magnitude ratio, line 210-232), and `specflux` (positive-half-rectified magnitude difference, line 235-243 — exactly the formula our existing `flux_stage` uses).

Defaults differ between use cases:
- `aubio_onset` (general onset detector, `onset.c:298`): default is **HFC** with logarithmic compression enabled (`aubio_onset_set_compression(o, 1.)`). Threshold 0.058.
- `aubio_tempo` (the beat tracker, `tempo.c:204-205`): default is **specflux**. Threshold 0.3, no compression by default.

The fact that aubio ships *different defaults for onset detection vs. tempo tracking* is itself a finding. HFC is sharper and works better for one-shot onset detection (it ignores low-frequency rumble from kicks the way our `flux_high` does); specflux is broader-band and gives a more stable periodic envelope for autocorrelation. BTrack uses CSD-HWR for both because it has only one ODF.

### b. Peak picking

`peakpicker.c:86-123` implements a moving-window adaptive threshold. Window: 5 post-samples + 1 pre-sample (`peakpicker.c:166-167` — the comment block at lines 39-44 says `[<----post----|--pre-->]` with `now` at the boundary). Per ODF sample:

1. Push the new sample into a `win_post + win_pre + 1 = 7`-sample ring (line 98).
2. Apply a 2nd-order Butterworth lowpass (cutoff 0.34 Nyquist, `peakpicker.c:181-184`) via forward-and-reverse filtering (`fvec_filtfilt`, line 103). This is zero-phase smoothing of the ODF tail.
3. Compute mean and median of the smoothed window (lines 106, 110).
4. Threshold: `thresholded = ODF[centre] - median - 0.1·mean` (line 116-117). The 0.1 is the user-tunable threshold offset.
5. Three-sample peak-pick (`fvec_peakpick`, line 119) on the *thresholded* signal. If a peak fires, refine to fractional-sample precision via parabolic interpolation of the three values (`fvec_quadratic_peak_pos`, line 121).

This is the formula our existing v2 research notes (research-notes.md §1) chose. Note that the `0.1` offset in the source is the default `t->threshold = 0.1` at `peakpicker.c:165`, but the public `aubio_onset` API overrides it per-method (e.g. 0.058 for HFC, 0.3 for tempo; see `onset.c:298-330`).

### c. Tempo estimation

The structure that holds the tempo state is `_aubio_beattracking_t` at `beattracking.c:32-56`. Key fields:
- `acf` — the autocorrelation of the current ODF frame.
- `acfout` — the comb-filterbank output (length `laglen = winlen/4`, ≈128).
- `rwv` — the **Rayleigh weighting** for the general state (line 101-104; `rayparam = 60·sr/120/hop` so the prior peaks at 120 BPM, identical to BTrack's choice).
- `gwv` — the **Gaussian weighting** for the context-dependent state (built when the state activates, `beattracking.c:361-363`, with `σ = g_var = 3.901` indices, derived empirically per the comment on line 78).
- `dfwv` — an **exponential weighting** applied to the ODF history before autocorrelation (lines 96-99). It rises from ~0 to 1 across the buffer with a half-life of 43 samples (≈ same as the Rayleigh peak); this biases beat-phase detection toward more recent samples.
- `phwv` — a **Gaussian phase weighting** rebuilt each frame in the context-dependent state (lines 372-377), centred on the predicted next beat with `σ² = bp/8`.

Window is `winlen = next_pow2(5.8·sr/hop)` ≈ 5.8 seconds (line 188), and beat tracking runs every `step = winlen/4` ODF samples (line 190 / 71), i.e. every ~1.5 s. The tempo loop is *not* per-hop — like BTrack it amortises the heavy work over many frames.

The same shift-invariant comb-filterbank as BTrack runs at `beattracking.c:163-170`: `acfout[i] += acf[i·a + b - 1] / (2a - 1)` for `a ∈ [1, numelem]` and `b ∈ [1, 2a)`, with `numelem = 4` (or the detected time signature, 3 or 4, when in context-dependent state). Then `fvec_weight(acfout, rwv)` applies the Rayleigh prior (line 172). The argmax + parabolic peak refinement (line 175-180) gives `bt->rp`, the Rayleigh-state beat period.

State transition is in `aubio_beattracking_checkstate` (`beattracking.c:291-415`). The logic: if the current beat-period observation differs from the running estimate by more than `2·g_var ≈ 7.8 lag samples`, set a "step change observed" flag and start a 3-frame counter. If the next two observations are consistent (`|2·rp - rp1 - rp2| < g_var`), commit to the new tempo and switch on the Gaussian context-dependent prior. The time signature (3 or 4) is then sniffed from the ACF at `beattracking.c:271-289` by comparing energy at `3·gp` vs `4·gp`.

Confidence: `aubio_beattracking_get_confidence` at `beattracking.c:439-449` returns `quadratic_peak_mag(acfout, gp) / sum(acfout)` — the fraction of the comb-filterbank's total energy concentrated in the winning peak (after parabolic refinement). This is in [0, 1] and is non-zero only in the context-dependent state. This is a much more honest confidence measure than `1 - second_best/best` because it reflects how *peaky* the tempo posterior is rather than just how separated the top two candidates are.

### d. Beat phase

Beat phase is a *per-block* (every 1.5 s) prediction, not a per-hop one. In `aubio_beattracking_do` at lines 198-225:
1. Reverse the (weighted) ODF buffer so "now" is at index 0.
2. Build `phout[i] = Σ_k dfrev[i + round(bp·k)]` for `i ∈ [0, bp)` and `k ∈ [0, kmax)` — i.e. fold the past `kmax` beat periods of the ODF over a single beat-period window. This is a phase histogram: bins where many past beats land sum constructively.
3. Multiply by `phwv` (the Gaussian phase prior; flat in the general state, narrow Gaussian in the context-dependent state).
4. `phase = quadratic_peak_pos(phout, argmax(phout)) + 1` is the offset of the next beat into the period.
5. The block then emits a *list* of beat times (line 247-263) at `beat`, `beat+bp`, `beat+2·bp`, … out to the end of the block, where `beat = bp - phase`. There's a guard for "next beat too close" at lines 240-245.

So aubio gives you, every 1.5 s, a vector of expected beat times across the next 1.5 s. Consumers iterate per-hop and check whether the current `blockpos` matches an expected beat (`tempo.c:89-100`). This is functionally equivalent to BTrack's `timeToNextBeat` countdown, but the prediction horizon is a full block ahead instead of one beat ahead.

---

## 3. Side-by-side comparison

| Aspect | BTrack | aubio |
|---|---|---|
| Default ODF | Complex spectral difference, half-wave rectified | `tempo`: spectral flux. `onset`: HFC with log-compression |
| ODF resampling | Resample buffer to 512 with libsamplerate every beat | None; uses raw ODF |
| Peak picker | Moving-window mean detrender, no separate peak-pick (peaks emerge via cumulative score) | Median + 0.1·mean over 7-sample window with 2nd-order Butterworth lowpass + parabolic refinement |
| Tempo method | FFT-based balanced ACF → 4-element shift-invariant comb filterbank → Rayleigh prior → 41-state Viterbi over tempo transitions | Direct ACF (`aubio_autocorr`) → 4-element shift-invariant comb filterbank → Rayleigh prior; Gaussian narrowing once locked |
| Tempo state | Continuous Viterbi smoothing; explicit `fixTempo` API for context-dependent override | Discrete two-state machine (general / context-dependent) gated by 3-frame consistency check |
| Beat tracker style | Causal cumulative-score search with log-Gaussian window `W1`; argmax of weighted future score over the next beat | Per-block phase folding + Gaussian phase prior; emits beat list across next ~1.5 s |
| Confidence read-out | None in public API (could be derived from `delta`) | `quadratic_peak_mag(acfout, gp) / sum(acfout)`, only meaningful in context-dependent state |
| Beat-phase prediction horizon | One beat period ahead, recomputed at the off-beat | One block (~1.5 s, multiple beats) ahead, recomputed every 1.5 s |
| Latency to lock | Dependent on `α` momentum and Viterbi state; effectively ~3 s | Hard-coded ~3-frame consistency check in `checkstate`, plus ~1 block ≈ 1.5 s |
| What the other doesn't | Forward-projected cumulative score for sub-beat prediction; tempo Viterbi | Multiple selectable ODFs; explicit confidence; time-signature detection (3 vs 4) |
| Comments are illuminating | Yes — Stark cites his own thesis equations inline (`BTrack.cpp:648, 661, 677`) | Mostly terse; the `g_var = 3.901; // constthresh empirically derived!` comment at `beattracking.c:78` is the kind of magic number that only a reference implementation tells you about |

---

## 4. Key algorithmic primitives to implement

These are the building blocks both libraries rely on, with their canonical literature references:

- **Complex spectral difference (CSD)** — per-bin Euclidean distance between actual and phase-extrapolated complex spectral values, optionally half-wave rectified on the magnitude term. Inputs: current and previous frame's complex FFT (or magnitude + phase + previous-previous phase). Output: one scalar per hop. Complexity: O(N) per hop after FFT. Ref: Bello, Daudet, Abdallah, Duxbury, Davies, Sandler, *A Tutorial on Onset Detection in Music Signals*, IEEE TSAP 2005. Original CSD: Duxbury, Bello, Davies, Sandler, *Complex Domain Onset Detection for Musical Signals*, DAFx 2003.

- **Spectral flux on log-magnitude** — what our `flux_stage` already computes. Cheap, broad-band, robust. Ref: Dixon, *Onset Detection Revisited*, DAFx 2006. (BTrack's `SpectralDifferenceHWR` and aubio's `specflux` are this.)

- **Half-wave rectification** — `max(0, x)`. Trivial. Used to discriminate onsets from offsets in any spectral-difference detector.

- **Adaptive thresholding (median + λ·mean)** — the aubio formula `t = median(window) + λ·mean(window)` over a 6-7-sample window centred on the current sample. Inputs: ODF stream. Output: thresholded ODF (clip to zero). Complexity: O(W log W) per sample if median is recomputed naively, O(log W) with a tracking-median data structure. Ref: Brossier, Bello, Plumbley, *Real-Time Temporal Segmentation of Note Objects in Music Signals*, ICMC 2004.

- **Refractory rule** — minimum inter-onset interval, typically 30-50 ms. Trivial. Ref: same Brossier 2004.

- **Balanced (FFT-based) autocorrelation** — `IFFT(|FFT(x)|²)` divided by `(N - lag)` to compensate for the smaller overlap at large lags. Inputs: 512-sample ODF buffer. Output: 512-lag ACF. Complexity: O(N log N). Ref: any DSP textbook; the Wiener-Khinchin theorem.

- **Shift-invariant comb filterbank** — for each candidate lag `τ`, sum the ACF at integer multiples `a·τ + b` for `a ∈ [1, A]` (typically 4) and `b ∈ [1-a, a-1]`, normalised by `1/(2a-1)`. Width-`(2a-1)` neighbourhoods make the filter tolerant of small tempo deviations and harmonic-comb errors. Inputs: ACF. Output: tempo-likelihood at each lag. Complexity: O(L · A²) where L is the number of candidate lags. Ref: Davies & Plumbley, *Context-Dependent Beat Tracking of Musical Audio*, IEEE TASLP 15(3), 2007.

- **Rayleigh tempo prior** — `w(τ) = (τ/β²)·exp(-τ²/(2β²))` with `β` chosen so the peak is at 120 BPM (in lag units, `β = 60·sr/(120·hop)`). Multiplies the comb-filter output. Inputs: lag axis. Output: weighting vector. Complexity: O(L) build, O(L) apply. Ref: Davies & Plumbley 2007 cite this; Stark thesis equation 3.7.

- **Gaussian tempo prior** — once a tempo is locked, replace the Rayleigh prior with a narrow Gaussian centred on the locked tempo. `σ ≈ 4-5 lag samples` empirically (aubio's `g_var = 3.901`). Ref: Davies & Plumbley 2007.

- **Two-state context-dependent beat tracker** — start in "general" state with Rayleigh prior; after observing a tempo that's stable for ≥ 3 ACF frames, switch to "context-dependent" state with Gaussian priors on tempo and phase. Ref: Davies & Plumbley 2007; companion paper Davies, *Towards Automatic Rhythmic Accompaniment*, PhD thesis QMUL 2007.

- **Cumulative-score beat tracker** (BTrack only) — `C[n] = (1-α)·ODF[n] + α·max_{k ∈ window} W1(k)·C[n-k]` with log-Gaussian window `W1(k) = exp(-½·(γ·log(k/β))²)`. The log-Gaussian is the *key* — it makes the window symmetric in musical (octave) time rather than linear time. Inputs: ODF stream. Output: beat-likelihood stream. Complexity: O(W) per sample where W ≈ 1.5·beat-period. Ref: Stark, Davies, Plumbley, *Real-Time Beat-Synchronous Analysis of Musical Audio*, DAFx 2009; Stark thesis 2011.

- **Forward-projected beat prediction** (BTrack only) — extend the cumulative score one beat period into the future using `α = 1` (pure momentum, no new ODF evidence), then argmax against a Gaussian beat-expectation window centred at half a beat period ahead. This gives a sub-hop time-to-next-beat. Ref: Stark thesis equations 3.4 and 3.6.

- **Parabolic peak refinement** — for an integer-indexed peak at `i`, fit `y(i-1), y(i), y(i+1)` to a parabola and report the vertex's fractional offset. Standard MIR trick; both libraries use it (`fvec_quadratic_peak_pos` in aubio, implicit nowhere in BTrack but useful for our confidence computation). Inputs: three values. Output: fractional offset. Complexity: O(1).

- **Tempo confidence as `peak_magnitude / sum`** — aubio's measure. Robust, dimensionless, in [0, 1], and goes to zero in the general state which is honest. Ref: not formally published; it's an aubio invention.

---

## 5. Implementation plan for Listeningway

### Recommended algorithmic choices

**ODF**: complex spectral difference, half-wave rectified (BTrack default). Reasons: (a) the half-wave rectification on the magnitude term gives a cleaner onset signal than aubio's plain complex-domain method; (b) it's a single ODF that works for both percussive and pitched material, where aubio needs to switch between HFC and specflux; (c) it requires phase, which forces us to retain phase from the FFT — see prerequisite refactor below.

**Tempo estimation**: shift-invariant comb filterbank with Rayleigh prior (both libraries agree on this). Run on a balanced FFT-based ACF of a 512-sample resampled ODF buffer (BTrack's approach — fixed sizes downstream regardless of host hop size).

**Tempo state machine**: 41-state Viterbi smoothing with Gaussian transition matrix (BTrack). Reason: continuous, no discrete state-switch latency, and the transition matrix is a cheap 41×41 lookup. aubio's discrete two-state machine has a hard-coded 3-frame lockup that adds ~4.5 s of latency to tempo changes — too sluggish for our use case.

**Confidence**: aubio's `quadratic_peak_mag(comb_output, peak_lag) / sum(comb_output)`. This is the single most important fix relative to the current implementation. Pin a `tempo_detected` threshold around 0.10-0.15 of this measure, *not* 0.4 of the current `1 - second_best/best`.

**Beat tracker**: BTrack's cumulative-score with log-Gaussian window `W1`, `α = 0.9` momentum, `γ = 5` tightness. Reason: this is exactly what we need for `beat` and `beat_phase` shader uniforms — a continuous beat-likelihood signal, plus forward-projected sub-hop time-to-next-beat. aubio's per-block beat-list approach gives chunky 1.5-s updates which would visibly judder the visualizer when tempo drifts.

**Beat-phase prediction**: BTrack's `predictBeat()` — synthesise `futureCumulativeScore` one beat period ahead with `α=1` and argmax against a Gaussian beat-expectation window. Use `timeToNextBeat / beatPeriod` as the `beat_phase` value, mapped to [0, 1) by the consumer. This is *strictly* a better contract for shader-driven visualisation than "beat just fired", because it gives us a smooth countdown between beats.

### Pre-requisite refactors

The CSD-HWR ODF needs FFT *phase*, not just magnitude. Today our pipeline only retains `Magnitudes` in `AnalysisFrame`. We need one of:

- **Option A**: add a `Phases` field to `AnalysisFrame` (parallel array, same size as magnitudes), and have the FFT stage populate it. Cheapest. Touches `audio/dsp/fields.h`, the FFT stage, and the new BeatStage. `flux_stage` is unaffected (it ignores phase).
- **Option B**: keep `Magnitudes` and add a separate `ComplexSpectrum` field (interleaved real/imag pairs). More flexible but doubles the spectral storage and forces both `flux_stage` and the new BeatStage through a `sqrt(re² + im²)` reconstruction. Not worth it.

Recommend Option A.

`FluxStage` does **not** need to change. Its `flux_total/low/mid/high` outputs remain useful for the per-band reactive uniforms; we just stop using them as the BeatStage's primary input. The BeatStage will read `Magnitudes` and `Phases` directly and compute its own ODF internally. (Alternatively the ODF could be hoisted into a dedicated `OdfStage` — see below.)

### File-by-file scope

- **New file**: `src/audio/dsp/beat/odf_csd.{h,cpp}` — ~80 LOC. Stateful CSD-HWR implementation taking `(magnitudes, phases)` and returning one ODF sample. Holds `prev_mag`, `prev_phase`, `prev_phase2`.
- **New file**: `src/audio/dsp/beat/odf_buffer.{h,cpp}` — ~60 LOC. Circular buffer of ODF samples sized for ~6 s of history at the running hop rate. Provides a "snapshot to fixed length 512" via linear (or libsamplerate-equivalent) resampling. Plain linear resampling is fine here — the ODF is already a slow, smoothed signal.
- **New file**: `src/audio/dsp/beat/comb_tempo.{h,cpp}` — ~150 LOC. Adaptive threshold, balanced FFT-ACF, shift-invariant comb filterbank, Rayleigh prior, 41-state Viterbi smoothing, confidence read-out. Depends on the project's existing FFT (KissFFT or pocketfft).
- **New file**: `src/audio/dsp/beat/beat_tracker.{h,cpp}` — ~120 LOC. Cumulative-score update, log-Gaussian window builder, `predictBeat` with forward projection. Owns `beatPeriod`, `timeToNextBeat`, `timeToNextPrediction`.
- **Modified**: `src/audio/dsp/stages/beat_stage.{h,cpp}` — replace internals; keep the outward `IDspStage` interface, the `Mode/Profile/Custom` switch (it now only affects the pulse-curve presentation, not the underlying detection), and the snapshot fields `beat_pulse_strength` / `beat_auto_locked`. ~200 LOC after the rewrite (down from ~300).
- **Modified**: `src/audio/dsp/fields.h` — add `Phases` field.
- **Modified**: FFT stage — populate `Phases`.

**Total new LOC**: ~600. **Modified LOC**: ~150. **Estimated effort**: 12-18 hours of focused engineering, including unit tests against synthetic click tracks at 60/100/120/140/170/200 BPM, plus a regression on a small set of recorded music excerpts. Two-thirds of the time will be in tuning the magic constants (`α = 0.9`, `γ = 5`, Rayleigh `β = 43`, transition `σ = 41/8`) — these are 44.1 kHz / hop=512 numbers and we run at variable hop, so we need to derive the right scaling.

### Open questions to resolve during implementation

- Should the tempo Viterbi's 41-state lattice (80-160 BPM in 2 BPM steps) be widened? Our use case (game soundtracks, dance music, electronic) probably wants 60-200 BPM with finer resolution. Wider lattice means a bigger transition matrix; not free but not expensive either.
- Resampling the ODF buffer: BTrack uses libsamplerate's SINC interpolator. We don't need that quality for an already-smoothed signal — linear interpolation will be fine and removes a dependency.
- The 41-state Viterbi could be replaced with a Kalman-style continuous tracker if we want continuous tempo output (it currently quantises to 2 BPM). Not in scope for v1.

---

## 6. License posture

BTrack is GPLv3. We are *studying* the algorithm — reading the source to understand the technique and the constants — and re-implementing from understanding. We are **not** copying source. The algorithmic lineage traces back through Stark's thesis (publicly available) and Davies & Plumbley's IEEE TASLP 2007 paper, both of which describe the same techniques. The clean-room rewrite cites these papers as the authoritative source. We retain no BTrack header, no BTrack class names, no BTrack variable names, no BTrack code structure beyond what is dictated by the published algorithm. The tempo Viterbi, the Rayleigh prior, the comb filterbank, and the cumulative-score formulation are all in the published literature; the *specific* constants (`α = 0.9`, `γ = 5`) come from the thesis, not the source.

aubio is BSD-3-Clause, which is permissive — we may borrow short snippets if helpful (e.g. the median-tracking adaptive threshold in `peakpicker.c:86-123` is small enough to copy verbatim with attribution), but we expect to re-implement everything to match our coding conventions. Where we adopt aubio's specific design choices (the `peak_magnitude / sum` confidence measure, the median + 0.1·mean threshold formula), we cite Brossier's 2004 ICMC paper and the aubio source as the lineage.

The reference papers that anyone reviewing the implementation should be able to look up:

- Stark, Davies, Plumbley, *Real-Time Beat-Synchronous Analysis of Musical Audio*, DAFx-09, Como, 2009.
- Stark, A.M., *Automatic Real-Time Beat Tracking*, PhD thesis, Queen Mary University of London, 2011.
- Davies, M.E.P., Plumbley, M.D., *Context-Dependent Beat Tracking of Musical Audio*, IEEE Transactions on Audio, Speech, and Language Processing, 15(3):1009-1020, 2007.
- Bello, Daudet, Abdallah, Duxbury, Davies, Sandler, *A Tutorial on Onset Detection in Music Signals*, IEEE Transactions on Speech and Audio Processing, 13(5):1035-1047, 2005.
- Duxbury, Bello, Davies, Sandler, *Complex Domain Onset Detection for Musical Signals*, DAFx-03, London, 2003.
- Brossier, Bello, Plumbley, *Real-Time Temporal Segmentation of Note Objects in Music Signals*, ICMC-04, Miami, 2004.
