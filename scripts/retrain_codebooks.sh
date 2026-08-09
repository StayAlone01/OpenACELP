#!/bin/sh
# ------------------------------------------------------------------
# retrain_codebooks.sh - retrain BOTH codebooks in one shot:
#   1. gain codebook (cl. 4.2.2.6) -> src/gain_codebook.c
#   2. LSP codebooks   (cl. 4.2.2.3) -> include/LSP_codebooks.h
# (LSP training runs AFTER gain training, as requested.)
#
# Each step builds the encoder in its own training mode (-DGAIN_TRAINING
# / -DLSP_TRAINING), collects the features from every .raw file in the
# directory (recursive), runs LBG and regenerates the C table. The normal
# encoder is rebuilt at the end.
#
# Usage:
#   sh scripts/retrain_codebooks.sh RAW_DIR \
#       [gain_features] [lsp_features] [gain_out] [lsp_out]
#
#   RAW_DIR      directory with 8 kHz 16-bit mono .raw files
#                (produce them from any audio with scripts/audio_to_raw.py)
#
# Optional overrides (defaults shown):
#   gain_features  /tmp/openacelp_features.txt
#   lsp_features   /tmp/openacelp_lsp_features.txt
#   gain_out       src/gain_codebook.c
#   lsp_out        include/LSP_codebooks.h
#
# ------------------------------------------------------------------
set -e

RAW_DIR="${1:?usage: sh scripts/retrain_codebooks.sh RAW_DIR [gain_feat] [lsp_feat] [gain_out] [lsp_out]}"
GAIN_FEAT="${2:-/tmp/openacelp_features.txt}"
LSP_FEAT="${3:-/tmp/openacelp_lsp_features.txt}"
GAIN_OUT="${4:-src/gain_codebook.c}"
LSP_OUT="${5:-include/LSP_codebooks.h}"

if [ ! -d "$RAW_DIR" ]; then
    echo "error: no such directory: $RAW_DIR" >&2
    exit 1
fi

echo "=================================================="
echo " [1/2] GAIN codebook (cl. 4.2.2.6)"
echo "=================================================="
sh scripts/retrain_gain_codebook.sh "$RAW_DIR" "$GAIN_FEAT" "$GAIN_OUT"

echo "=================================================="
echo " [2/2] LSP codebooks (cl. 4.2.2.3)"
echo "=================================================="
sh scripts/retrain_lsp_codebook.sh "$RAW_DIR" "$LSP_FEAT" "$LSP_OUT"

echo "=================================================="
echo "All codebooks retrained and normal encoder rebuilt."
echo "Validate (held-out files):"
echo "  make clean && make CFLAGS=\"-Wall -Wextra -O2 -std=c99 -DGAIN_DEBUG -DLSP_DEBUG\""
echo "  for f in held_out/*.raw; do ./openacelp \"\$f\" > /tmp/out_\$(basename \$f).txt 2>&1; done"
echo "  cat /tmp/out_*.txt > /tmp/out_all.txt"
echo "  python3 scripts/validate_gains.py /tmp/out_all.txt"
echo "  python3 scripts/validate_lsp.py  /tmp/out_all.txt"
echo "  make clean && make"
echo "=================================================="
