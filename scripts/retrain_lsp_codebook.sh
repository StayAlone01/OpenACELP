#!/bin/sh
# ------------------------------------------------------------------
# retrain_lsp_codebook.sh - one-shot LSP codebook retraining
# (cl. 4.2.2.3). Runs the full pipeline: build the encoder in training
# mode, collect the 10-D cosine-domain LSP vectors from every .raw file
# in a directory, run LBG on the 3 split groups (3/3/4 dims, 256/512/512
# entries) and regenerate src/lsp_codebook.c, then rebuild the normal
# encoder. The extern declarations live in include/LSP_codebooks.h and
# are hand-maintained (they do not change across retrains).
#
# Usage:
#   sh scripts/retrain_lsp_codebook.sh RAW_DIR [features_file] [out_file] [restarts]
#
#   RAW_DIR      directory with 8 kHz 16-bit mono .raw files
#                (produce them from any audio with scripts/audio_to_raw.py)
#   features_file  default: /tmp/openacelp_lsp_features.txt
#   out_file     default: src/lsp_codebook.c
#   restarts     number of LBG restarts (--restarts, default 1)
#
# ------------------------------------------------------------------
set -e

RAW_DIR="${1:?usage: sh scripts/retrain_lsp_codebook.sh RAW_DIR [features] [out] [restarts]}"
FEAT="${2:-/tmp/openacelp_lsp_features.txt}"
OUT="${3:-src/lsp_codebook.c}"
RESTARTS="${4:-1}"

if [ ! -d "$RAW_DIR" ]; then
    echo "error: no such directory: $RAW_DIR" >&2
    exit 1
fi

echo "[1/4] building the encoder in training mode (-DLSP_TRAINING)"
make clean >/dev/null
make CFLAGS="-Wall -Wextra -O2 -std=c99 -DLSP_TRAINING" >/dev/null

echo "[2/4] collecting LSP features from $RAW_DIR"
: > "$FEAT"
for f in $(find "$RAW_DIR" -name '*.raw' | sort); do
    ./openacelp "$f" 2>> "$FEAT" > /dev/null
done

echo "[3/4] LBG (ordering-constrained Lloyd, $RESTARTS restart(s)) -> $OUT  ($(wc -l < "$FEAT") frames)"
TMPOUT="$OUT.tmp.$$"
python3 scripts/lsp_codebook_generator.py --restarts "$RESTARTS" "$FEAT" > "$TMPOUT"
mv -f "$TMPOUT" "$OUT"

echo "[4/4] rebuilding the normal encoder"
make clean >/dev/null
make >/dev/null

echo "done. Next: validate with scripts/validate_lsp.py "
echo "(make CFLAGS=\"... -DLSP_DEBUG\", run ./openacelp on held-out files,"
echo " python3 scripts/validate_lsp.py lsp.txt, then make clean && make)"
