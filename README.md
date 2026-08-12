# OpenACELP
Free ACELP vocoder. It is based on **ETSI EN 300-395-2**<sup>[1]</sup> and **TIA/EIA IS-641**<sup>[2]</sup>, but it is **not** compatible with any of them (as their codebooks can't be published as a part of this codec). **OpenACELP** is an alternative to (great) [Codec 2](https://github.com/drowe67/codec2). It uses floating point arithmetic. I aim to optimize it for the STM32 Cortex-M7, as they have a hardware floating point unit (FPU).

The codebooks are trained on **LibriSpeech `train-clean-360`** (~360 h, CC BY 4.0) with the project's own numpy-vectorized Linde–Buzo–Gray (LBG) implementation (`scripts/lbg_common.py`). A codebook is specific to the corpus it was trained on — see `docs/codebook_training.md` for the training pipeline.

## Codec parameters

| Parameter | Value |
|-----------|-------|
| Coding model | ACELP (Code-Excited Linear Prediction), floating point |
| Sampling rate | 8 kHz |
| Frame length | 30 ms / 240 samples |
| Look-ahead | 40 samples (LPC analysis window: 280 samples) |
| Sub-frame length | 4 × 7.5 ms / 60 samples |
| Bitrate | **4 567 bit/s** = 137 bits per 30 ms frame |
| LPC order | 10 (Levinson–Durbin, 60 Hz bandwidth expansion) |
| LP representation | LSPs in cosine domain, Chebyshev polynomial root search |
| LSP quantization | Split-VQ: 3 codebooks (3 + 3 + 4 dims, 256/512/512 entries) → **26 bits/frame** |
| LSP quality (validated) | Held-out LibriSpeech `dev-clean`: **0.00 % ordering fallbacks, 1.44 dB mean spectral distortion** |
| Gain quality (validated) | Held-out LibriSpeech `dev-clean`: 64/64 codebook usage, mean \|log₂ error\| ≈ 0.26 bit |
| LSP interpolation | Per sub-frame: 100/0, 75/25, 50/50, 25/75 (this/previous frame) |
| Perceptual weighting | `W(z) = A(z/γ3) / A(z/γ4)` with γ3 = 0.95, γ4 = 0.60 |
| Shaping matrix | `F(z) = A(z/γ1) / A(z/γ2)` with γ1 = 0.75, γ2 = 0.85 (planned, [1] annex F) |
| Open-loop pitch | Once per frame, 3 search ranges: 20–39, 40–79, 80–142 |
| Closed-loop pitch | Per sub-frame, limited window around the open-loop pitch; integer search + ±1/3, ±2/3 fractional refinement (cl. 4.2.2.4) |
| Algebraic codebook | 16-bit, 4 pulses (+√2, −1, +1, −1), dynamic shaping `F(z)=A(z/0.75)/A(z/0.85)`, focused search (cl. 4.2.2.5) |
| Gain quantization | 2-D log2-energy VQ of `gp`/`gc`, 64-entry codebook (6 bits × 4 sub-frames = 24 bits/frame), cross-coupled energy prediction (cl. 4.2.2.6) |
| Target platform | STM32 Cortex-M7 (hardware FPU) |

## Status

See [TODO.md](TODO.md) for the full implementation checklist.

Implemented so far (encoder):

- Pre-processing (offset compensation + divide by 2)
- Framing and modified Hamming windowing
- Autocorrelation with bandwidth expansion
- Levinson–Durbin LP solver with stability fallback
- LP ↔ LSP conversion (Chebyshev polynomials, root search)
- LSP split-vector quantization + per-sub-frame interpolation
- Perceptual weighting filter
- Open-loop pitch search
- Closed-loop (adaptive) pitch search — per sub-frame, fractional resolution (cl. 4.2.2.4)
- Algebraic (innovative) codebook search — 4 pulses, dynamic shaping, focused search (cl. 4.2.2.5)
- Gain prediction + vector quantization of `gp`/`gc` — 2-D VQ, 6 bits/sub-frame, interleaved per sub-frame (cl. 4.2.2.6)
- Bit allocation & multiplexing — 137-bit frame packing, MSB-first, table 3 order (cl. 4.2.2.7)

Decoder (cl. 4.2.3):

- De-multiplexing + parameter decoding (LSP, pitch, algebraic code, gains)
- Excitation reconstruction + synthesis `1/A(z)` (cl. 4.2.3.1)
- Post-processing (×2 with saturation, cl. 4.2.1)
- Error concealment on `BFI` (cl. 4.2.3.2)

Not yet implemented: channel coding (cl. 5) — deferred.

## Implementation notes (cl. 4.2.2.4)

- The fractional pitch refinement follows the standard's method: the normalized
  correlation (eq. 24) is interpolated with the 8-tap filter and its maximum is
  searched; the encoder does not brute-force every 1/3-step candidate.
- The closed-loop search uses the **quantized** LP parameters for the weighting and
  synthesis filters (`W(z) = A(z)/A(z/0.85)`, `1/A(z/0.85)`), as cl. 4.1 requires.
  The open-loop stage still uses the un-quantized `A(z/0.95)/A(z/0.60)` filter, as
  cl. 4.2.2.4 specifies.

## Implementation notes (cl. 4.2.2.5)

- The codebook is searched by maximizing `C²/ε` (eq. 27–29) over the 4-pulse
  algebraic structure, with the dynamic shaping filter `F(z) = A(z/0.75)/A(z/0.85)`
  combined into the weighted-synthesis impulse response. The fixed-gain pitch
  contribution (0.8, for T < 60) is applied to the shaping impulse response, as
  cl. 4.2.2.5 NOTE 4 requires.
- The focused-search thresholds use the exact maximum 2- and 3-pulse correlations
  found prior to the search; the worst-case time counter (350) bounds the search
  (cl. 4.2.2.5).
- The provisional codebook gain `gc = C/ε` (eq. 30) is computed and stored; it is
  now quantized jointly with the pitch gain by the 4.2.2.6 stage.

## Implementation notes (cl. 4.2.2.6)

- `gp` and `gc` are quantized jointly in the log2-energy domain with a 64-entry 2-D
  codebook (6 bits per sub-frame, 24 bits per frame). The energies are computed in
  the **natural float log2 domain** (no fixed-point scaling offsets):
  `e_p = log2(E_p·E_lpc)`, `e_c = log2(E_c·E_lpc)`, where `E_lpc` is the energy of
  the impulse response of `1/A(z)`.
- The prediction uses the **last quantized** energies, cross-coupled
  (`0.5·last_pit + 0.25·last_cod − 3.0`, clamped at 0); the 2-D search minimizes the
  squared error in the log2 domain.
- The quantized energies are limited to **27 dB (pitch) / 25 dB (code)** before the
  gains are reconstructed, as cl. 4.2.2.6 requires.
- The quantized gains build the true excitation `u = gp·v + gc·c'`, which updates the
  adaptive-codebook memory **per sub-frame** (pitch → codebook → gains → excitation
  update are interleaved), replacing the earlier LP-residual placeholder.
- The gain codebook in `src/gain_codebook.c` is trained on **LibriSpeech
  `train-clean-360`** (~360 h, CC BY 4.0). To retrain or extend it, see
  `docs/codebook_training.md` (`make CFLAGS="... -DGAIN_TRAINING"` collects the
  features, then `scripts/gain_codebook_generator.py`).
- The LSP codebooks (`src/lsp_codebook.c`, declared `extern` in
  `include/LSP_codebooks.h`) are likewise trained on LibriSpeech
  `train-clean-360`; retrain with `sh scripts/retrain_lsp_codebook.sh <raw_dir>` (or
  retrain **both** in one go with `sh scripts/retrain_codebooks.sh <raw_dir>` — LSP
  first, then gain).

## Building

```sh
make
```

The binary takes a RAW file (signed 16-bit, little-endian, 8 kHz) and runs the encoder on it:

```sh
./openacelp speech.raw
```

## Training the codebooks

One-shot retraining of both codebooks (gain then LSP) from a directory of 8 kHz
16-bit mono `.raw` files:

```sh
python3 scripts/audio_to_raw.py <audio_dir> <raw_dir> --recursive   # any format -> 8 kHz RAW
sh scripts/retrain_codebooks.sh <raw_dir>
```

Per-codebook quick paths:

```sh
# gain only
sh scripts/retrain_gain_codebook.sh <raw_dir>
# LSP only
sh scripts/retrain_lsp_codebook.sh <raw_dir>
```

Manual gain path (what `retrain_gain_codebook.sh` does):

```sh
make clean && make CFLAGS="-Wall -Wextra -O2 -std=c99 -DGAIN_TRAINING"
for f in <raw_dir>/*.raw; do ./openacelp "$f" 2>> features.txt > /dev/null; done
python3 scripts/gain_codebook_generator.py features.txt > src/gain_codebook.c
make clean && make
```

Validate with `scripts/validate_gains.py` (build with `-DGAIN_DEBUG` first) and
`scripts/validate_lsp.py` (build with `-DLSP_DEBUG` first).

## Project structure

```
src/       codec sources (main, preprocess, lpc, lsp, gain, codebook, pitch, openacelp)
include/   public/internal headers, gamma tables, LSP codebooks
scripts/   codebook training & validation tools (audio conversion, LBG, generators, validators)
```

## References

[1] ETSI EN 300-395-2 — TETRA and Critical Communications Evolution (TCCE); Speech codec for full-rate traffic channel; Part 2: TETRA codec.

[2] TIA/EIA IS-641 — TDMA Enhanced Full Rate speech codec.
