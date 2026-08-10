#!/bin/sh
# ------------------------------------------------------------------
# retrain_codebooks.sh - retrain BOTH codebooks in one shot, LSP first:
#   1. LSP codebooks   (cl. 4.2.2.3) -> src/lsp_codebook.c
#   2. gain codebook   (cl. 4.2.2.6) -> src/gain_codebook.c (two-pass)
#
# LSP is trained FIRST: the LSP features do not depend on the gain
# codebook, while the gain codebook is trained through the quantized A(z)
# built from the LSP codebook. Each LSP group is clustered with the
# shared LBG using the ordering-constrained Lloyd and --restarts N
# (default 4). The normal encoder is rebuilt after the LSP step (so the
# new LSP table is active), and the gain codebook is then retrained with
# the two-pass, quantized-feedback pipeline
# (scripts/retrain_gain_codebook_2pass.sh) against that final LSP codebook.
#
# Usage:
#   sh scripts/retrain_codebooks.sh RAW_DIR \
#       [lsp_features] [gain_features_p1] [gain_features_p2] \
#       [lsp_out] [gain_out] [restarts]
#
#   RAW_DIR      directory with 8 kHz 16-bit mono .raw files
#                (produce them from any audio with scripts/audio_to_raw.py)
#
# Optional overrides (defaults shown):
#   lsp_features      /tmp/openacelp_lsp_features.txt
#   gain_features_p1  /tmp/gain_features_pass1.txt
#   gain_features_p2  /tmp/gain_features_pass2.txt
#   lsp_out           src/lsp_codebook.c
#   gain_out          src/gain_codebook.c
#   restarts          LBG restarts for the LSP codebooks (4)
#
# ------------------------------------------------------------------
set -e

RAW_DIR="${1:?usage: sh scripts/retrain_codebooks.sh RAW_DIR [lsp_feat] [g1] [g2] [lsp_out] [gain_out] [restarts]}"
LSP_FEAT="${2:-/tmp/openacelp_lsp_features.txt}"
GAIN_FEAT1="${3:-/tmp/gain_features_pass1.txt}"
GAIN_FEAT2="${4:-/tmp/gain_features_pass2.txt}"
LSP_OUT="${5:-src/lsp_codebook.c}"
GAIN_OUT="${6:-src/gain_codebook.c}"
RESTARTS="${7:-4}"

if [ ! -d "$RAW_DIR" ]; then
    echo "error: no such directory: $RAW_DIR" >&2
    exit 1
fi

echo "=================================================="
echo " [1/2] LSP codebooks (cl. 4.2.2.3)"
echo "=================================================="
sh scripts/retrain_lsp_codebook.sh "$RAW_DIR" "$LSP_FEAT" "$LSP_OUT" "$RESTARTS"

echo "=================================================="
echo " [2/2] gain codebook, two-pass (cl. 4.2.2.6)"
echo "       (trained against the new LSP codebook from step 1)"
echo "=================================================="
sh scripts/retrain_gain_codebook_2pass.sh "$RAW_DIR" "$GAIN_FEAT1" "$GAIN_FEAT2" "$GAIN_OUT"

echo "=================================================="
echo "All codebooks retrained (LSP first, then gain) and"
echo "the normal encoder is rebuilt."
echo "Validate (held-out files):"
echo "  make clean && make CFLAGS=\"-Wall -Wextra -O2 -std=c99 -DGAIN_DEBUG -DLSP_DEBUG\""
echo "  for f in held_out/*.raw; do ./openacelp \"\$f\" > /tmp/out_\$(basename \$f).txt 2>&1; done"
echo "  cat /tmp/out_*.txt > /tmp/out_all.txt"
echo "  python3 scripts/validate_gains.py /tmp/out_all.txt"
echo "  python3 scripts/validate_lsp.py  /tmp/out_all.txt"
echo "  make clean && make"
echo "=================================================="
