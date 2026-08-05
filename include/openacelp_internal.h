#ifndef OPENACELP_INTERNAL_H
#define OPENACELP_INTERNAL_H

//Standard includes
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

//Global consts
#define FRAME_SIZ		240							//voice frame samples number, 0.03s*8000Hz
#define LOOK_AHEAD		40							//40 samples of look-ahead for the LPC analysis
#define WINDOW_SIZ		(FRAME_SIZ+LOOK_AHEAD)		//window for LPC analysis samples number
#define SUBFRAME_SIZ	(FRAME_SIZ/4)				//subframe length in samples
#define ALPHA			(float)32735.0/32768.0		//alpha coeff for the pre-processing filter
#define GRID_SIZ		60							//grid granularity for LSP computation

//#define DEBUG										//comment it out later
//#define OVF_INFO
#define ERRORS

//-----------------------------Global vars (extern)-----------------------------
extern float		w[WINDOW_SIZ];					//modified Hamming window w(n) coeffs for speech analysis
extern float		grid[GRID_SIZ];					//grid of values for LSP computation

extern int16_t		prev_spch_frame[FRAME_SIZ];		//previous speech frame
extern int16_t		prev_w_spch_frame[FRAME_SIZ];	//previous speech frame (weighted)

extern uint64_t		frame;							//frame number - for our info

//-----------------------------Function declarations------------------------------
//preprocess
void Speech_Pre_Process(int16_t *inp, int16_t *outp);

//lpc
void Analysis_Window_Init(float *w);
void Window_Speech(int16_t *inp, int16_t *outp);
void Autocorr(int16_t *spch, int32_t *r);
void LD_Solver(int32_t *r, float *a);

//lsp
void Grid_Generate(float *g);
float Chebyshev_Eval(float x, float *f);
void LP_LSP(float *prev_LSP, float *a, float *LSP);
void LSP_SVQ(float *lsp, float *q_lsp, uint16_t *ind);
void LSP_Poly(float *lsp, float *f);
void LSP_LP(float *lsp, float *a);
void Init_LSP(float *in1, float *in2);

//openacelp (top-level encode + helpers)
void Filter(int16_t *out, int16_t *inp, float *b, float *a, uint8_t len, int16_t *prev_s, int16_t *prev_w_s);
void Speech_Weighting(int16_t *spch_out, int16_t *spch_in, float a[][11]);
uint8_t Find_Pitch(int16_t *spch, int16_t *prev_s_w);

#endif
