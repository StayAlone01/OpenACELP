# Codebook training guide

> **Current state (2026-08-09):** both codebooks have been retrained on
> **LibriSpeech train-clean-100** (~100 h, 28 539 files) and are installed in the
> tree (`src/gain_codebook.c`, `src/lsp_codebook.c`). Measured on the training
> data: the LSP codebooks quantize with **~1.45 dB mean spectral distortion and
> 0.05 % ordering fallbacks**; the gain codebook has **~0.47 bit mean quantization
> error, 64/64 entries used**. This replaces the previous 65 s demo gain codebook
> and the 2020 TED-LIUM LSP codebooks. Held-out validation on LibriSpeech
> `dev-clean` (not used in training): LSP **0.05 % ordering fallbacks / 1.43 dB mean
> spectral distortion**, gain **RESULT OK** (64/64 usage, mean |log2 error| ≈ 0.3 bit).

This guide explains how to (re)train the OpenACELP codebooks — currently the
**gain codebook** (cl. 4.2.2.6, `src/gain_codebook.c`) and, in the same spirit, the
**LSP codebooks** (cl. 4.2.2.3, `src/lsp_codebook.c`). Both are now trained on
**LibriSpeech `train-clean-100`** (~100 h, CC BY 4.0) — see the status note above.
The pipeline below lets you reproduce that training or extend it (e.g. with more
LibriSpeech data). A codebook is specific to the corpus it was trained on — using a
different corpus produces a different, non-interchangeable codebook.

> **Licensing first.** A codebook is *derived data*: distributing it means distributing
> derivative work of the training corpus. This project's codebooks are trained on
> **LibriSpeech `train-clean-100`** (CC BY 4.0), a permissive license — see the status
> note above.

---

## 1. Prerequisites

- `gcc`, `make`, `python3`
- `numpy` (used by the resampler and the LBG): `python3 -m pip install --user numpy`
- `soundfile` (FLAC/OGG/MP3 decoding without ffmpeg):
  `python3 -m pip install --user "soundfile==0.12.1"`
  > **Version note:** soundfile ≥ 0.13 requires numpy ≥ 1.22. If your system numpy is
  > older (e.g. 1.21.x), pin `soundfile==0.12.1`, which bundles libsndfile and works
  > with older numpy.
- `ffmpeg` (optional but recommended — handles every audio format):
  `sudo apt install ffmpeg` (Debian/Ubuntu)

> Run all scripts with the **same python3 that has numpy** (e.g. `/usr/bin/python3`).
> Do not use a workspace virtualenv that lacks numpy (such as a ROS2 `.venv`).

---

## 2. Gain codebook — full retraining pipeline

The gain codebook is a 64-entry 2-D VQ (6 bits/sub-frame) of the pitch and codebook
gain *prediction errors* in the natural float log2-energy domain. Training is a 4-step
loop: **prepare audio → collect features → LBG → validate**.

### Step 1 — Prepare the corpus (any format → 8 kHz, 16-bit, mono RAW)

```sh
# Put your audio files (flac/wav/mp3/...) anywhere, then:
python3 scripts/audio_to_raw.py /path/to/audio_files/ /path/to/raw_out/
```

`audio_to_raw.py` uses `ffmpeg` when available, otherwise a numpy-based band-limited
resampler for WAV files. Output: one `<name>.raw` (signed 16-bit LE, 8 kHz) per input.

### Step 2 — Build the encoder in training mode

```sh
make clean && make CFLAGS="-Wall -Wextra -O2 -std=c99 -DGAIN_TRAINING"
```

In this mode `src/gain.c` skips the VQ, drives the prediction state with the
*unquantized* energies and prints one `err_pit err_cod` pair per sub-frame on
**stderr**. (Gains pass through unquantized so the encoder keeps running normally.)

### Step 3 — Collect the feature vectors

```sh
mkdir -p training && rm -f training/features.txt
for f in /path/to/raw_out/*.raw; do
    ./openacelp "$f" 2>> training/features.txt > /dev/null
done
```

(Note the `> /dev/null`: the encoder prints the open-loop pitch on stdout — you do
*not* want ~12 M lines of that in your terminal.) This yields one line per 7.5 ms
sub-frame (~480 k lines per hour of speech). **Do not filter the silence** — quiet
frames must stay in the data (see the note in `gain_codebook_generator.py`: without
them the gain reconstruction blows up on silence). A few hundred k lines is plenty.

Or run the whole thing (build training mode + collect + LBG + rebuild) in one shot:

```sh
sh scripts/retrain_gain_codebook.sh /path/to/raw_out/
```

### Recommended: two-pass (feedback-matched) training

`-DGAIN_TRAINING` drives the prediction state with *unquantized* energies, but at
runtime the state is driven by the *quantized* energies — so a single-pass
codebook is trained under a slightly different prediction trajectory than it runs
under. Use the two-pass script instead: it trains a pass-1 table, then re-collects
the features through the normal quantized-feedback path (`-DFEEDBACK_TRAINING`
hook in `src/gain.c`) with that table in place, and retrains to the final table:

```sh
sh scripts/retrain_gain_codebook_2pass.sh /path/to/raw_out/
```

This is the recommended path for production codebooks; it costs roughly 2x the
feature-collection time of the single pass.

### Step 4 — Run LBG and regenerate the codebook

```sh
python3 scripts/gain_codebook_generator.py training/features.txt > src/gain_codebook.c
```

This clusters the features into 64 2-D vectors with a dependency-free Linde–Buzo–Gray
implementation (same algorithm as the `py-lbg` tool used for the LSP codebooks) and
writes the C table. `--clip-lo` / `--clip-hi` are optional (only remove absurd
outliers; default: no clipping).

### Step 5 — Rebuild the normal encoder

```sh
make clean && make
```

### Step 6 — Validate

```sh
# build with the debug hook, run on a few held-out files, then:
make clean && make CFLAGS="-Wall -Wextra -O2 -std=c99 -DGAIN_DEBUG"
./openacelp /path/to/held_out.raw > /tmp/gains.txt 2>&1
python3 scripts/validate_gains.py /tmp/gains.txt
make clean && make
```

`validate_gains.py` reports: gain tracking error (dB) for `gp` and `gc`, codebook
usage (distinct indices / 64), gain bounds (`gp_q ≤ 1.2`, no NaN), and any
`gc_q` over-shoot. A good result: mean |log2 error| ≲ 0.3, most entries used,
silence frames → gains near zero, `gp_q` never above 1.2.

> **Two-pass refinement (optional):** the first training pass uses *unquantized*
> energies for the prediction state. For the best match to runtime, retrain once more
> with the trained codebook in place (normal build) — i.e. repeat steps 2–4 without
> `-DGAIN_TRAINING`? No — repeat the feature collection with the *quantized* feedback:
> build normally, run the encoder, and this time capture features by temporarily
> enabling a `-DFEEDBACK_TRAINING` hook (not yet implemented) or by accepting the
> small mismatch. In practice one pass is fine; the two-pass refinement is optional.

### Overnight one-shot run (LibriSpeech train-clean-100)

Everything below runs unattended. On an 8-core machine expect roughly **2–3 h** total
and **≤ ~1.5 GB RAM** (measured: 5 M features → 838 MB peak, ~4 min LBG; the 100 h
run is ~48 M features → ~1 GB peak, ~40 min LBG).

```sh
# 0) one-time prerequisites (system python3, not a ROS2 .venv)
python3 -m pip install --user "soundfile==0.12.1"

# 1) download + extract LibriSpeech (https://www.openslr.org/12)
mkdir -p ~/libri && cd ~/libri
wget -c https://www.openslr.org/resources/12/train-clean-100.tar.gz
tar xzf train-clean-100.tar.gz            # -> LibriSpeech/train-clean-100/<book>/<chapter>/*.flac

# 2) convert everything to 8 kHz RAW (recursive scan, flat output)
cd /home/garyluk/OpenACELP
python3 scripts/audio_to_raw.py ~/libri/LibriSpeech/train-clean-100 ~/libri/raw --recursive

# 3) train BOTH codebooks (gain first, then LSP) — or just the gain one:
sh scripts/retrain_codebooks.sh ~/libri/raw
#   (gain only:  sh scripts/retrain_gain_codebook.sh ~/libri/raw
#    LSP only:   sh scripts/retrain_lsp_codebook.sh  ~/libri/raw )

# 4) validate on a HELD-OUT set (dev-clean is not used for training):
#    download + convert it the same way as train-clean-100
cd ~/libri
wget -c https://www.openslr.org/resources/12/dev-clean.tar.gz
tar xzf dev-clean.tar.gz                  # -> LibriSpeech/dev-clean/...
cd /home/garyluk/OpenACELP
python3 scripts/audio_to_raw.py ~/libri/LibriSpeech/dev-clean ~/libri_dev_raw --recursive

# 5) run the debug hooks on the held-out files and validate both codebooks
make clean && make CFLAGS="-Wall -Wextra -O2 -std=c99 -DGAIN_DEBUG -DLSP_DEBUG"
for f in ~/libri_dev_raw/*.raw; do ./openacelp "$f" > /tmp/out_$(basename $f).txt 2>&1; done
cat /tmp/out_*.txt > /tmp/out_all.txt
python3 scripts/validate_gains.py /tmp/out_all.txt
python3 scripts/validate_lsp.py  /tmp/out_all.txt
make clean && make
```

`audio_to_raw.py --recursive` writes all RAW files flat into one directory (safe for
LibriSpeech — its file names are unique). All `retrain_*.sh` scripts scan
recursively. If the process is interrupted, nothing is lost: re-running `--recursive`
skips already-converted files, and the feature files can simply be appended to.

> **Measured (2026-08-09, train-clean-100):** 28 539 files, 12 054 124 LSP frames
> (~100.4 h) and 48 216 496 gain sub-frames (exactly 4× the frame count — the two
> feature sets are internally consistent). Both debug hooks print to **stderr**, so
> the `2>&1` capture above cannot interleave them mid-line.

Timing note: the combined run collects *two* feature sets (gain sub-frames +
LSP frames) and runs LBG three more times (256/512/512) — plan for roughly
1.5–2× the gain-only runtime (still comfortably overnight; RAM stays ~1 GB).

---

## 3. Gain validation checklist

- [ ] Build clean (`make`), no new warnings from gain code
- [ ] Encoder runs on voiced speech / silence / noise without NaN
- [ ] `gp_q` in [0, 1.2], `gp_q ≈ gp` on voiced frames (≤ ~0.3 log2 error)
- [ ] `gc_q` tracks `gc` (mean ≤ ~0.3 log2 error; no systematic over/under-estimate)
- [ ] Silence frames → `gp_q` and `gc_q` near 0 (no explosion)
- [ ] Codebook usage: most of the 64 entries are selected across the corpus
- [ ] Round-trip sanity: excitation `u = gp·v + gc·c'` amplitude is in the same range
      as the LP residual (no runaway)

---

## 4. LSP codebooks — retraining (same idea, different features)

The LSP codebooks are retrained with the same **automated** pipeline as the gain
codebook (installed from LibriSpeech `train-clean-100`). The features are the
**raw LSP vectors** (the split codebooks model the LSP distribution directly),
10 values in the cosine domain `q_k = cos(w_k)`, one line per frame.

### Step 1 — Build the encoder in LSP training mode

```sh
make clean && make CFLAGS="-Wall -Wextra -O2 -std=c99 -DLSP_TRAINING"
```

`-DLSP_TRAINING` adds a dump in `LSP_SVQ` (`src/lsp.c`): every frame prints its
10-D LSP vector to **stderr** (the encoder keeps running normally).

### Step 2 — Collect the feature vectors

```sh
mkdir -p training && rm -f training/lsp_features.txt
for f in /path/to/raw_out/*.raw; do
    ./openacelp "$f" 2>> training/lsp_features.txt > /dev/null
done
```

One line per 30 ms frame (~120 k lines per hour of speech).

### Step 3 — Run LBG and regenerate the codebooks

```sh
python3 scripts/lsp_codebook_generator.py training/lsp_features.txt > src/lsp_codebook.c
```

This splits each 10-D vector into the 3 standard groups (3 + 3 + 4), runs the
shared numpy LBG (`scripts/lbg_common.py`) with the standard sizes
**256 / 512 / 512** and emits `src/lsp_codebook.c` (the `extern` declarations
live in `include/LSP_codebooks.h` and never change). The ordering constraint is
enforced **inside every LBG iteration** (each centroid is projected onto the
valid LSP space — components in [-1, 1], strictly decreasing — during the Lloyd
updates, not just as a post-hoc cleanup), so every emitted entry is a **valid LSP
sub-vector**. `--restarts N` runs the LBG from several seeded starting points and
keeps the lowest-distortion result (helps the 512-entry codebooks escape shallow
local minima; a few restarts are cheap relative to the full 100+ h run):

```sh
python3 scripts/lsp_codebook_generator.py --restarts 4 \
    training/lsp_features.txt > src/lsp_codebook.c
```

### Step 4 — Rebuild and validate

```sh
make clean && make CFLAGS="-Wall -Wextra -O2 -std=c99 -DLSP_DEBUG"
for f in /path/to/held_out/*.raw; do ./openacelp "$f" > /dev/null 2>> lsp_debug.txt; done
python3 scripts/validate_lsp.py lsp_debug.txt
make clean && make
```

`validate_lsp.py` reports the per-group quantization error (cosine RMS and Hz
RMS), codebook usage per group, the **spectral distortion** (mean/max dB — the
standard LSP quality metric; < 1 dB mean is very good, < 2 dB acceptable), and
the ordering-fallback rate. It uses an exact Python port of the encoder's
`LSP_Poly`/`LSP_LP` (verified against C) so the distortion is trustworthy.

### One-shot scripts

```sh
# LSP codebooks only
sh scripts/retrain_lsp_codebook.sh /path/to/raw_out/

# BOTH codebooks: gain first, then LSP (each rebuilds the encoder in its own
# training mode), normal encoder rebuilt at the end
sh scripts/retrain_codebooks.sh /path/to/raw_out/
```

### Volume note

The LSP codebooks need proportionally more data than the 64-entry gain
codebook — tens of hours of speech (tens of millions of frames). The chunked
LBG handles that in ~1 GB of RAM, same as the gain run. The old
`scripts/q_codebook_generator.py` (py-lbg on pre-built `Q1/Q2/Q3` files) is
superseded by this pipeline.

> **Observed reality check (2026-08-09, LibriSpeech-trained codebooks):**
> the codec2 test files (`david4`, `vk2tpm`) are **radio-processed audio** and are
> pathological for LSP split-VQ — the LibriSpeech codebook gives ~100 % ordering
> fallbacks and ~8 dB spectral distortion on them. They are **not** a valid quality
> test for the LSP codebooks. On proper held-out speech (LibriSpeech `dev-clean`,
> not used in training) it measures **0.05 % fallbacks and 1.43 dB mean spectral
> distortion**. **Always validate on held-out speech** (e.g. LibriSpeech
> `dev-clean`), not on these radio files.

> **Debug-stream note:** `GAIN_DEBUG` (src/gain.c) and `LSP_DEBUG` (src/lsp.c)
> both print to **stderr**, so capturing encoder output with `2>&1` into one file
> is safe — the validators filter by content (`gp_q` / `LSP` prefixes). If they
> ever split mid-line, run the encoder with stdout and stderr to separate files.

---

## 5. Practical tips

- **Data volume:** the gain codebook converges quickly — even 5 h of clean speech
  gives a large improvement; the LSP codebooks want 20 h+. Quality over quantity:
  clean, varied, correctly-leveled speech beats noisy mass.
- **Level:** the codec expects 16-bit speech at normal recording level. If your
  corpus is very quiet/loud, gain-normalise it (ffmpeg `loudnorm` or `volume`) so the
  trained codebook matches real usage.
- **Keep silence** in the training features (see note above).
- **Held-out validation:** always validate on files **not** used for training.
- **Startup frames:** the first ~1 s of each file contains filter warm-up; harmless,
  but you can trim it per file if you like.
- **RAM / overnight runs (measured on an i7-10700, 32 GB):** the encoder itself
  streams (a few MB). The numpy LBG on 5 M points (≈10 h of speech) peaks at
  **~0.8 GB** and takes ~4 min; for 100 h of speech (~48 M sub-frames, ~2.2 GB
  features file) extrapolate to **~1 GB peak and ~40 min** — 32 GB is far more than
  enough, no OOM risk. Timing: encoder feature collection runs at ~120x real time
  (100 h -> ~50 min of CPU). The whole train-clean-100 run fits comfortably
  overnight.
- **Open items**: the codebook-related open items are resolved (LibriSpeech codebooks
  installed); mark the remaining TODO items done.
