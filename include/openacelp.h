#ifndef OPENACELP_H
#define OPENACELP_H

#include <stdint.h>

//initialize codec: search grid, analysis window, filter memory
//arg1: root search grid, arg2: modified Hamming window
//arg3,4: memory for speech weighting filter
void ACELP_Init(float *search_grid, float *analysis_window, int16_t *f_mem1, int16_t *f_mem2);

//encode voice frame
//arg1: input speech samples, 16-bit signed integer (this frame)
//arg2: 137 unpacked output bits
void ACELP_EncodeFrame(int16_t *speech, uint8_t *out);

#endif
