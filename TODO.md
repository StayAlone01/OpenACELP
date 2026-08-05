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
- [ ] Verify LPC analysis is bit/behaviourally correct vs. reference (only partially validated)

## 3. LP ↔ LSP conversion ([1] cl. 4.2.2.2, [2] cl. 2.2.3)

- [x] LP → LSP in cosine domain (`LP_LSP`)
- [x] Chebyshev polynomial evaluation + root search (`Chebyshev_Eval`, `LSP_Poly`)
- [x] LSP → LP (`LSP_LP`)

## 4. Quantization & interpolation of LP parameters ([1] cl. 4.2.2.3, [2] cl. 2.2.3)

- [x] Split vector quantization of LSPs (3 codebooks: 3+3+4, 8/9/9 bits = 26 bits) — full search (`LSP_SVQ`)
- [x] Quantized & unquantized LSP interpolation per sub-frame
- [ ] **Retrain / finalize codebooks** — current ones are a *test* set trained on ~44 min of TED-LIUM (FrankGehry_1990.sph only). Needs much more training data + validation for decent quality

## 5. Perceptual weighting ([1] cl. 4.1, 4.2.2; `W(z) = A(z/γ1)/A(z/γ2)`, γ1=0.85, γ2=0.85)

- [x] Weighting filter for all 4 sub-frames (`Speech_Weighting`, `Filter`) — "(WIP) looks like it works"
- [ ] Confirm against reference implementation; current code uses unquantized LSPs for weighting, check requirement that weighting uses **quantized** LP params

## 6. Long-term prediction analysis ([1] cl. 4.2.2.4, [2] cl. 2.3)

- [x] Open-loop pitch search, once per frame (`Find_Pitch`, 3 ranges 20–39 / 40–79 / 80–142) — `(WIP)`
- [ ] **Fix TODO in `Find_Pitch`** — normalization loop is wrong (iterates over full frame with `ind[i]` thresholds instead of the 120-sample subset used in `C[k]` accumulation; pitch range boundary also questionable)
- [ ] Closed-loop (adaptive codebook) search, once per sub-frame — **NOT implemented**
  - [ ] Adaptive codebook construction from past excitation (interpolation for fractional/whole delays; repetition when delay < sub-frame length)
  - [ ] Sub-frame adaptive codebook search with perceptually weighted synthesis (`MSE search`)
  - [ ] Pitch delay coding (whole/fractional parts, as per [1] bit allocation — 7 bits/sub-frame)

## 7. Algebraic (innovative) codebook ([1] cl. 4.2.2.5)

- [ ] Define algebraic codebook structure (track positions, pulse positions/amplitudes) per [1]
- [ ] Implement dynamic shaping matrix `F(z) = A(z/γ1)/A(z/γ2)` with **γ1 = 0.75, γ2 = 0.85** (Toeplitz lower-triangular shaping, [1] annex F) — note: this differs from the weighting filter γ
- [ ] Algebraic codebook search (per sub-frame, analysis-by-synthesis, MSE search)
- [ ] Codebook index coding (per [1] bit allocation)

## 8. Gain quantization ([1] cl. 4.2.2.6)

- [ ] Gain prediction (prediction from past gains / excitation energy)
- [ ] Vector quantization of `gp` (pitch gain) and `gc` (codebook gain)
- [ ] Gain codebook(s) training (same pipeline as LSP codebooks: py-lbg on TED-LIUM)

## 9. Bit allocation & multiplexing ([1] cl. 4.2.2.7, table 1/3 — 137 bits / 30 ms)

- [ ] Bit-packing of all parameters into the 137-bit frame
- [ ] Output bit stream from `ACELP_EncodeFrame` (currently the `out` parameter is unused/NULL)
- [ ] Frame format documentation (bit order, table 3 of [1])

## 10. Decoder ([1] cl. 4.2.3, [2] cl. 2.6)

- [ ] De-multiplexing of the 137-bit frame
- [ ] Decoding of LP filter parameters (`LSP_Az` inverse of `Az_Lsp`; interpolation per sub-frame)
- [ ] Decoding of the adaptive codebook vector
- [ ] Decoding of the innovation vector (algebraic codebook + shaping)
- [ ] Decoding of adaptive & innovative codebook gains
- [ ] Computation of reconstructed speech:
  - [ ] Long-term synthesis filter `1/(1 − gp·z⁻ᵀ)`
  - [ ] Short-term synthesis filter `1/A(z)`
- [ ] Post-processing (×2 with saturation)
- [ ] Error concealment (`BFI` handling, [1] cl. 4.2.3.2)

## 11. Channel coding / decoding ([1] cl. 5 & 6) — for a full TETRA-compatible bit stream

- [ ] CRC codes (speech frame classes)
- [ ] 16-state RCPC mother code of rate 1/3
- [ ] Puncturing schemes: rate 8/12 (2/3), 8/18; rate 8/17 for frame stealing mode
- [ ] Matrix interleaving
- [ ] Channel decoder: de-interleaving + Viterbi decoding, BFI generation ([1] annex A)

## 12. Integration, validation & tooling

- [ ] Decoder + encoder in a single working pipeline (encode → channel → decode)
- [ ] Round-trip / listening tests with TED-LIUM corpus
- [ ] Compare with reference: SNR, PESQ-style metrics, bit-exactness where applicable
- [ ] WAV/Raw file I/O polish; CLI encode/decode tools
- [ ] STM32 Cortex-M7 (FPU) optimization pass + profiling
- [ ] `scripts/` cleanup: LBG (`lbg.c`, `q_codebook_generator.py`) and `sound_process.sh` integration with final codebooks

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
| **Closed-loop adaptive codebook search** | **missing** |
| **Algebraic codebook + shaping matrix** | **missing** |
| **Gain prediction & VQ** | **missing** |
| **Bit allocation / multiplexer (137 bits)** | **missing** |
| **Decoder (all sub-blocks + error concealment)** | **missing** |
| **Channel coding (CRC, RCPC, interleaving)** | **missing** |
| **Post-processing (decoder)** | **missing** |
