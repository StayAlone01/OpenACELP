#!/bin/sh
# ------------------------------------------------------------------
# retrain_gain_codebook_2pass.sh - two-pass gain codebook retraining
# (cl. 4.2.2.6).
#
# Why two passes: the -DGAIN_TRAINING hook (pass 1) drives the gain
# prediction state with the UNQUANTIZED energies, which makes collecting
# features cheap and stable, but the codebook then runs under a slightly
# different prediction trajectory than at runtime (where the state is
# driven by the QUANTIZED energies). Pass 2 rebuilds with the pass-1
# codebook in place and re-collects the features through the normal
# quantized-feedback path (-DFEEDBACK_TRAINING), so the final codebook is
# trained on the exact error distribution it will encounter at runtime.
#
# Pipeline:
#   pass 1: build -DGAIN_TRAINING  -> collect features -> LBG -> codebook
#   pass 2: build -DFEEDBACK_TRAINING (normal path, codebook in place)
#           -> collect features    -> LBG -> final codebook
#   rebuild the normal encoder.
#
# Usage:
#   sh scripts/retrain_gain_codebook_2pass.sh RAW_DIR \
#        [features_pass1] [features_pass2] [out_file]
#
#   RAW_DIR         directory with 8 kHz 16-bit mono .raw files
#                   (produce them from any audio with scripts/audio_to_raw.py)
#   features_pass1  default: /tmp/gain_features_pass1.txt
#   features_pass2  default: /tmp/gain_features_pass2.txt
#   out_file        default: src/gain_codebook.c
#
# ------------------------------------------------------------------
set -e

RAW_DIR="${1:?usage: sh scripts/retrain_gain_codebook_2pass.sh RAW_DIR [feat1] [feat2] [out]}"
FEAT1="${2:-/tmp/gain_features_pass1.txt}"
FEAT2="${3:-/tmp/gain_features_pass2.txt}"
OUT="${4:-src/gain_codebook.c}"

if [ ! -d "$RAW_DIR" ]; then
    echo "error: no such directory: $RAW_DIR" >&2
    exit 1
fi

# ---- pass 1: unquantized feedback (stable initial codebook) -------------
echo "[1/6] pass 1: building the encoder with -DGAIN_TRAINING"
make clean >/dev/null
make CFLAGS="-Wall -Wextra -O2 -std=c99 -DGAIN_TRAINING" >/dev/null

echo "[2/6] pass 1: collecting gain features from $RAW_DIR"
: > "$FEAT1"
for f in $(find "$RAW_DIR" -name '*.raw' | sort); do
    ./openacelp "$f" 2>> "$FEAT1" > /dev/null
done
echo "      $(wc -l < "$FEAT1") sub-frames collected"

echo "[3/6] pass 1: LBG -> $OUT (temporary pass-1 table)"
TMPOUT="$OUT.tmp.$$"
python3 scripts/gain_codebook_generator.py "$FEAT1" > "$TMPOUT"
mv -f "$TMPOUT" "$OUT"

# ---- pass 2: quantized feedback with the pass-1 codebook in place ------
echo "[4/6] pass 2: building the encoder with -DFEEDBACK_TRAINING"
make clean >/dev/null
make CFLAGS="-Wall -Wextra -O2 -std=c99 -DFEEDBACK_TRAINING" >/dev/null

echo "[5/6] pass 2: collecting gain features (quantized feedback) from $RAW_DIR"
: > "$FEAT2"
for f in $(find "$RAW_DIR" -name '*.raw' | sort); do
    ./openacelp "$f" 2>> "$FEAT2" > /dev/null
done
echo "      $(wc -l < "$FEAT2") sub-frames collected"

echo "[6/6] pass 2: LBG -> $OUT (final codebook)"
python3 scripts/gain_codebook_generator.py "$FEAT2" > "$TMPOUT"
mv -f "$TMPOUT" "$OUT"

# ---- rebuild the normal encoder ----------------------------------------
echo "      rebuilding the normal encoder"
make clean >/dev/null
make >/dev/null

echo "done (2 passes). Validate with scripts/validate_gains.py:"
echo "  make clean && make CFLAGS=\"-Wall -Wextra -O2 -std=c99 -DGAIN_DEBUG\""
echo "  ./openacelp held_out.raw > gains.txt 2>&1"
echo "  python3 scripts/validate_gains.py gains.txt"
echo "  make clean && make"
