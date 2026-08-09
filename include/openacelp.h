#ifndef OPENACELP_H
#define OPENACELP_H

#include <stdint.h>

// Initialize codec: search grid, analysis window, filter memory
// Arg1: root search grid, arg2: modified Hamming window
// Arg3,4: memory for speech weighting filter
void ACELP_Init(float *search_grid, float *analysis_window, int16_t *f_mem1, int16_t *f_mem2);

// Encode voice frame
// Arg1: input speech samples, 16-bit signed integer (this frame)
// Arg2: 137 unpacked output bits
void ACELP_EncodeFrame(int16_t *speech, uint8_t *out);

// Initialize the decoder state
void ACELP_Decoder_Init(void);

// Decode voice frame
// Arg1: 137 unpacked input bits (one bit per byte, as produced by the encoder)
// Arg2: bad frame indicator (1 = frame lost/corrupted -> error concealment)
// Arg3: output reconstructed speech samples, 16-bit signed integer
void ACELP_DecodeFrame(const uint8_t *bits, uint8_t bfi, int16_t *synth);

#endif
