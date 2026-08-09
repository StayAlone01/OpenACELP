#!/bin/sh
# ------------------------------------------------------------------
# retrain_gain_codebook.sh - one-shot gain codebook retraining
# (cl. 4.2.2.6). Runs the full pipeline: build the encoder in training
# mode, collect the (err_pit, err_cod) features from every .raw file in
# a directory, run LBG, regenerate src/gain_codebook.c and rebuild the
# normal encoder.
#
# Usage:
#   sh scripts/retrain_gain_codebook.sh RAW_DIR [features_file] [out_file]
#
#   RAW_DIR      directory with 8 kHz 16-bit mono .raw files
#                (produce them from any audio with scripts/audio_to_raw.py)
#   features_file  default: /tmp/openacelp_features.txt
#   out_file     default: src/gain_codebook.c
#
# ------------------------------------------------------------------
set -e

RAW_DIR="${1:?usage: sh scripts/retrain_gain_codebook.sh RAW_DIR [features] [out]}"
FEAT="${2:-/tmp/openacelp_features.txt}"
OUT="${3:-src/gain_codebook.c}"

if [ ! -d "$RAW_DIR" ]; then
    echo "error: no such directory: $RAW_DIR" >&2
    exit 1
fi

echo "[1/4] building the encoder in training mode (-DGAIN_TRAINING)"
make clean >/dev/null
make CFLAGS="-Wall -Wextra -O2 -std=c99 -DGAIN_TRAINING" >/dev/null

echo "[2/4] collecting gain features from $RAW_DIR"
: > "$FEAT"
for f in $(find "$RAW_DIR" -name '*.raw' | sort); do
    ./openacelp "$f" 2>> "$FEAT" > /dev/null
done

echo "[3/4] LBG -> $OUT  ($(wc -l < "$FEAT") sub-frames)"
TMPOUT="$OUT.tmp.$$"
python3 scripts/gain_codebook_generator.py "$FEAT" > "$TMPOUT"
mv -f "$TMPOUT" "$OUT"

echo "[4/4] rebuilding the normal encoder"
make clean >/dev/null
make >/dev/null

echo "done. Next: validate with scripts/validate_gains.py "
echo "(make CFLAGS=\"... -DGAIN_DEBUG\", run ./openacelp on held-out files,"
echo " python3 scripts/validate_gains.py gains.txt, then make clean && make)"
