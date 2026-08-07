#ifndef OPENACELP_INTERNAL_H
#define OPENACELP_INTERNAL_H

// Standard includes
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI			3.14159265358979323846
#endif

//-----------------------------ACELP includes & defines------------------------------
#include "gamma.h"
#include "LSP_codebooks.h"

// Global consts
#define FRAME_SIZ		  240							// Voice frame samples number, 30ms * 8000Hz = 240 samples
#define L1_SIZ	      216             // 216 samples from the present frame for the LPC analysis
#define LOOK_AHEAD		    40							// 40 samples of look-ahead for the LPC analysis
#define WINDOW_SIZ		(FRAME_SIZ+LOOK_AHEAD)		// 280: encoder input buffer (present frame + look-ahead)
#define LPC_WINDOW_SIZ	(L1_SIZ+LOOK_AHEAD)			// 256: LPC analysis window
#define LPC_WINDOW_OFF	(FRAME_SIZ-L1_SIZ)			// 24: window start offset inside the input buffer
#define SUBFRAME_SIZ	(FRAME_SIZ/4)				// Subframe length in samples
#define ALPHA			    (float)32735.0/32768.0		// Alpha coeff for the pre-processing filter
#define GRID_SIZ		  60							// Grid granularity for LSP computation


//#define DEBUG										// Comment it out later
//#define OVF_INFO
#define ERRORS

//-----------------------------Global vars (extern)-----------------------------
extern float		w[LPC_WINDOW_SIZ];				// Modified Hamming window w(n) coeffs for speech analysis
extern float		grid[GRID_SIZ+1];				// Grid of values for LSP computation
												// (+1 for the w=pi endpoint q=-1.0: the grid spans
												//  q=1.0..-1.0 in GRID_SIZ equal steps, as in the
												//  reference grid[grid_points+1])

extern int16_t		prev_spch_frame[FRAME_SIZ];		// Previous speech frame
extern int16_t		prev_w_spch_frame[FRAME_SIZ];	// Previous speech frame (weighted)

extern uint64_t		frame;							// Frame number - for our info

//-----------------------------Function declarations------------------------------
// Preprocess
void Speech_Pre_Process(int16_t *inp, int16_t *outp);

// Lpc
void Analysis_Window_Init(float *w);
void Window_Speech(int16_t *inp, int16_t *outp);
void Autocorr(int16_t *spch, int32_t *r);
void LD_Solver(int32_t *r, float *a);

// Lsp
void Grid_Generate(float *g);
float Chebyshev_Eval(float x, float *f);
void LP_LSP(float *prev_LSP, float *a, float *LSP);
void LSP_SVQ(float *lsp, float *q_lsp, uint16_t *ind);
void LSP_Poly(float *lsp, float *f);
void LSP_LP(float *lsp, float *a);
void Init_LSP(float *in1, float *in2);

// Openacelp (top-level encode + helpers)
void Filter(int16_t *out, int16_t *inp, float *b, float *a, uint8_t len, int16_t *prev_s, int16_t *prev_w_s);
void Speech_Weighting(int16_t *spch_out, int16_t *spch_in, float a[][11]);
uint8_t Find_Pitch(int16_t *spch, int16_t *prev_s_w);

#endif
