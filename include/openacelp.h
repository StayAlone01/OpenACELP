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

#endif
