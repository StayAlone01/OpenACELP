# OpenACELP - TODO

Status of the codec implementation against **ETSI EN 300-395-2** (TETRA codec, [1]) and **TIA/EIA IS-641** ([2]).

> **Project phase:** picked up by VR2YEP — development in progress (irregular). Encoder is partially implemented (up to open-loop pitch search). Everything below marked `[ ]` is **not done yet**.

Legend:
- `[x]` — implemented
- `[ ]` — **pending / not implemented**
- `(WIP)` — implemented but with known issues / unfinished details

---

## 1. Pre-processing / Post-processing ([1] cl. 4.2.1, [2] cl. 2.2)

- [x] Offset compensation + division by 2 (`Speech_Pre_Process`, `ALPHA = 32735/32768`)
- [ ] Decoder post-processing: multiply reconstructed speech by 2 with saturation control

## 2. Short-term (LPC) analysis ([1] cl. 4.2.2.1, [2] cl. 2.2.1–2.2.2)

- [x] Framing: 30 ms frames / 240 samples, 4 sub-frames of 60 samples
- [x] Modified Hamming window (`Analysis_Window_Init`, `Window_Speech`)
- [x] Autocorrelation with 60 Hz bandwidth expansion (`Autocorr`)
- [x] Levinson-Durbin solver, filter stability fallback (`LD_Solver`)
- [ ] Verify LPC analysis is bit/behaviourally correct vs. the standard (only partially validated)

## 3. LP ↔ LSP conversion ([1] cl. 4.2.2.2, [2] cl. 2.2.3)

- [x] LP → LSP in cosine domain (`LP_LSP`)
- [x] Chebyshev polynomial evaluation + root search (`Chebyshev_Eval`, `LSP_Poly`)
- [x] LSP → LP (`LSP_LP`)

## 4. Quantization & interpolation of LP parameters ([1] cl. 4.2.2.3, [2] cl. 2.2.3)

- [x] Split vector quantization of LSPs (3 codebooks: 3+3+4, 8/9/9 bits = 26 bits) — full search (`LSP_SVQ`)
- [x] Quantized & unquantized LSP interpolation per sub-frame
- [x] LSP codebook retraining pipeline (`scripts/lsp_codebook_generator.py`, `-DLSP_TRAINING`
      feature collection, shared numpy LBG to 256/512/512; `sh scripts/retrain_lsp_codebook.sh`,
      or both codebooks in one go with `sh scripts/retrain_codebooks.sh`)
- [ ] **Retrain / finalize LSP codebooks** — current ones are a *test* set trained on ~44 min of
      TED-LIUM (FrankGehry_1990.sph only). Pipeline is ready; run it on a proper multi-hour corpus
      (LibriSpeech train-clean-100) and validate with `scripts/validate_lsp.py`
      (measured: ~100 % ordering fallbacks / ~8 dB spectral distortion on codec2 test files today)

## 5. Perceptual weighting ([1] cl. 4.1, 4.2.2; `W(z) = A(z/γ1)/A(z/γ2)`, γ1=0.85, γ2=0.85)

- [x] Open-loop weighting filter for all 4 sub-frames (`Speech_Weighting`, `Filter`): `W(z) = A(z/0.95)/A(z/0.60)`, un-quantized LP (per cl. 4.2.2.4)
- [x] Closed-loop perceptual weighting uses **quantized** LP (`W(z)=A(z)/A(z/0.85)`, `1/A(z/0.85)`) in `src/pitch.c` (per cl. 4.1)

## 6. Long-term prediction analysis ([1] cl. 4.2.2.4, [2] cl. 2.3)

- [x] Open-loop pitch search, once per frame (`Find_Pitch`, 3 ranges 20–39 / 40–79 / 80–142)
- [x] Find_Pitch normalization uses the 120-sample stride-2 window matching the correlation numerator
- [x] Closed-loop (adaptive codebook) search, once per sub-frame (`Pitch_Analysis` in `src/pitch.c`)
  - [x] Search limited to a window around the open-loop pitch (sub-frame 1: ±2; sub-frames 2–4: `T1−5 … T1+4`), bounded by [20, 143] (cl. 4.2.2.4, NOTE 2)
  - [x] Adaptive codebook construction from past excitation (32-tap Hamming-windowed sinc interpolation for fractional delays; extension by LP residual when delay < 60)
  - [x] Sub-frame search maximizing eq. (24); ±1/3, ±2/3 refinement via 8-tap interpolation of the normalized correlation
  - [x] Pitch gain (eq. 25), clamped to [0, 1.2]
  - [x] Pitch delay coding: 8 bits (sf1) + 5 bits (sf2–4), own index mapping, stored in `Pitch_State.pitch_idx`
  - [x] Replace the LP-residual placeholder excitation with the true quantized excitation `u = gp·v + gc·c'` (done with cl. 4.2.2.6)

## 7. Algebraic (innovative) codebook ([1] cl. 4.2.2.5)

- [x] Define algebraic codebook structure (4 pulses, positions/amplitudes per table 2) (`src/codebook.c`)
- [x] Dynamic shaping `F(z) = A(z/0.75)/A(z/0.85)` combined with the weighted synthesis filter; fixed-gain pitch contribution (0.8, T<60) applied to the shaping impulse response (NOTE 4)
- [x] Algebraic codebook search per sub-frame: backward filtering, `Φ = HᵗH`, maximize `C²/ε` (eq. 27–29), focused search (0.586 thresholds, time counter 350)
- [x] Codebook index coding (14-bit index, table 4 layout; global sign + shift bits)
- [x] Provisional codebook gain `gc = C/ε` (eq. 30) stored in `Codebook_State` — now the input to the 4.2.2.6 gain VQ

## 8. Gain quantization ([1] cl. 4.2.2.6)

- [x] Gain prediction: per sub-frame, cross-coupled energy prediction
      `pred = 0.5·last_pit + 0.25·last_cod − 3.0` (clamped ≥ 0) from the last
      QUANTIZED energies, in the natural float log2 domain (no scaling offsets, D2)
- [x] Vector quantization of `gp` (pitch gain) and `gc` (codebook gain): 2-D VQ,
      64-entry codebook, 6 bits/sub-frame, minimum squared error in log2 domain
      (`src/gain.c`, `Gain_State`)
- [x] Quantized gains: `gp_q = min(2^(0.5·(last_pit−e_p)), 1.2)`, `gc_q = 2^(0.5·(last_cod−e_c))`;
      no energy limiting (D3 — not in the standard)
- [x] Interleaved per-sub-frame encode loop: pitch → codebook → gains → excitation
      update `u = gp·v + gc·c'` feeds the adaptive-codebook memory (D5); the
      weighted-synthesis filter memory is driven by the residual error `res − u`
- [x] Gain codebook training pipeline (`scripts/gain_codebook_generator.py`,
      `-DGAIN_TRAINING` feature collection, LBG to 64 entries)
- [x] Retrain the gain codebook on a proper corpus - Retrained on LibriSpeech.

## 9. Bit allocation & multiplexing ([1] cl. 4.2.2.7, table 1/3 — 137 bits / 30 ms)

- [x] Bit-packing of all parameters into the 137-bit frame (`src/bits.c`:
      `Prm_Pack` / `Prm_Unpack`, MSB-first, table 3 order)
- [x] Output bit stream from `ACELP_EncodeFrame` (`out` receives the 137
      unpacked bits)
- [x] Frame format documentation (bit order, table 3 of [1])
- [ ] Channel coding (cl. 5): CRC + RCPC + interleaving, sensitivity classes
      per table 4 (bits B1–B137); implement after the decoder (section 10) is done

## 10. Decoder ([1] cl. 4.2.3)

- [x] De-multiplexing of the 137-bit frame (`Prm_Unpack` in `src/bits.c`)
- [x] Decoding of LP filter parameters (`LSP_Decode` in `src/lsp.c`; interpolation per sub-frame)
- [x] Decoding of the adaptive codebook vector (fractional, repetition for T < 60; `Pitch_Adaptive_Sample` in `src/pitch.c`)
- [x] Decoding of the innovation vector (algebraic codebook + shaping; `Codebook_Decode_Sub` in `src/codebook.c`)
- [x] Decoding of adaptive & innovative codebook gains (`Gain_Decode_Sub` in `src/gain.c`)
- [x] Computation of reconstructed speech:
  - [x] Excitation `u = gp·v + gc·c'` (adaptive codebook + algebraic code)
  - [x] Short-term synthesis filter `1/A(z)` (`src/decode.c`)
- [x] Post-processing (×2 with saturation, `src/decode.c`)
- [x] Error concealment (`BFI` handling, [1] cl. 4.2.3.2)
- [x] Decode CLI mode + round-trip test (`src/main.c`, `tests/test_codec.c`)

## 11. Channel coding / decoding ([1] cl. 5 & 6) — **Deferred**: optional for a
clean-channel codec; implement after the decoder (section 10) and only if a real /
noisy-channel transmission or TETRA interop is needed.

- [ ] CRC codes (speech frame classes)
- [ ] 16-state RCPC mother code of rate 1/3
- [ ] Puncturing schemes: rate 8/12 (2/3), 8/18; rate 8/17 for frame stealing mode
- [ ] Matrix interleaving
- [ ] Channel decoder: de-interleaving + Viterbi decoding, BFI generation ([1] annex A)

## 12. Integration, validation & tooling

- [ ] Decoder + encoder in a single working pipeline (encode → channel → decode)
- [ ] Round-trip / listening tests with TED-LIUM corpus
- [ ] Compare with the standard: SNR, PESQ-style metrics, bit-exactness where applicable
- [ ] WAV/Raw file I/O polish; CLI encode/decode tools
- [ ] STM32 Cortex-M7 (FPU) optimization pass + profiling
- [ ] `scripts/` cleanup: LBG (`lbg.c`, `q_codebook_generator.py`) and `sound_process.sh` integration with final codebooks
- [ ] Optimization for different platforms (memory footprint, speed, etc.)
---

## Quick summary of remaining work

| Block | Status |
|-------|--------|
| Pre-processing (encoder) | done |
| LPC analysis (window, autocorr, Levinson-Durbin) | done |
| LP↔LSP, Chebyshev root search | done |
| LSP split-VQ + interpolation | done (codebooks need retraining) |
| Perceptual weighting | done *(needs verification)* |
| Open-loop pitch search | done *(has a TODO bug)* |
| Closed-loop adaptive codebook search | done (`src/pitch.c`) |
| Algebraic codebook + shaping matrix | done (`src/codebook.c`) |
| **Gain prediction & VQ** | **done** (`src/gain.c`, 2-D VQ, 6 bits × 4; codebook needs retraining) |
| **Bit allocation / multiplexer (137 bits)** | **missing** |
| **Decoder (all sub-blocks + error concealment)** | **missing** |
| **Channel coding (CRC, RCPC, interleaving)** | **missing** |
| **Post-processing (decoder)** | **missing** |
