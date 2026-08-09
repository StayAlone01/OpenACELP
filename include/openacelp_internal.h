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
#include "gain_codebook.h"

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
												//  q=1.0..-1.0 in GRID_SIZ equal steps, the "60 points"
												//  equally spaced between 0 and pi of cl. 4.2.2.2)

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
void LSP_SVQ(float *lsp, float *q_lsp, uint16_t *ind, float *q_lsp_prev);
void Int_LSP(float *lsp_prev, float *lsp_this, float lsp_int[4][10]);
void LSP_Poly(float *lsp, float *f);
void LSP_LP(float *lsp, float *a);
void Init_LSP(float *in1, float *in2);

//-----------------------------Pitch (cl. 4.2.2.4)-----------------------------
#define MAX_PITCH       143							// Max pitch delay (samples)
#define MIN_PITCH       20							// Min pitch delay (samples)
#define EXC_MEM         (MAX_PITCH + 32 + SUBFRAME_SIZ)	// Excitation buffer: max delay + 32-tap filter reach + sub-frame
#define NUM_PITCH_SUB   4							// Pitch analysis runs once per sub-frame

typedef struct
{
	float exc_buf[EXC_MEM];					// Past excitation, newest samples at the end
											// (placeholder: LP residual until 4.2.2.5/6 land)
	float w_mem[10];						// W(z) denominator memory (weighted speech history)
	float f_mem[10];						// Weighted synthesis filter 1/A(z/0.85) memory
	float s_mem[10];						// Past pre-processed speech (for the LP residual)
	int16_t T1;								// Integer part of sub-frame 1 pitch lag
	uint16_t pitch_idx[NUM_PITCH_SUB];		// Encoded pitch indices (our mapping, 8+5+5+5 bits)
	float gp[NUM_PITCH_SUB];				// Pitch gains per sub-frame
	float v[NUM_PITCH_SUB][SUBFRAME_SIZ];	// Adaptive codebook vectors per sub-frame
	int16_t T0[NUM_PITCH_SUB];				// Integer pitch delay per sub-frame
	float x2[NUM_PITCH_SUB][SUBFRAME_SIZ];	// Innovation target x2 = x - gp*y (eq. 26)
} Pitch_State;

void Pitch_Interp_Init(void);
void Pitch_Init(Pitch_State *ps);
void Pitch_Analysis_Sub(Pitch_State *ps, const int16_t *sprime, const float *Aq,
                        int16_t T0_ol, int sub, float *res);
void Excitation_Update(Pitch_State *ps, const float *Aq, const float *res,
                       const float *v, const float *c, float gp_q, float gc_q);

//-----------------------------Algebraic codebook (cl. 4.2.2.5)-----------------------------
typedef struct
{
	uint16_t code_idx[NUM_PITCH_SUB];		// 14-bit algebraic index (table 4 layout)
	uint8_t  sign[NUM_PITCH_SUB];			// Global sign bit
	uint8_t  shift[NUM_PITCH_SUB];			// Shift bit
	float gc[NUM_PITCH_SUB];				// Provisional codebook gain (eq. 30)
	float c[NUM_PITCH_SUB][SUBFRAME_SIZ];	// Shaped code vector c'(n)
	float y2[NUM_PITCH_SUB][SUBFRAME_SIZ];	// Filtered shaped code (for the gain)
} Codebook_State;

void Codebook_Init(Codebook_State *cs);
void Codebook_Analysis_Sub(Codebook_State *cs, const Pitch_State *ps,
                           const float *Aq, int sub);

//-----------------------------Gain quantization (cl. 4.2.2.6)-----------------------------
typedef struct
{
	float	last_ener_pit;				// Last QUANTIZED pitch energy (log2 domain)
	float	last_ener_cod;				// Last QUANTIZED code energy (log2 domain)
	uint8_t	gain_idx[NUM_PITCH_SUB];	// 6-bit gain codebook indices (4 per frame)
	float	gp[NUM_PITCH_SUB];			// Quantized pitch gains (final)
	float	gc[NUM_PITCH_SUB];			// Quantized code gains (final)
} Gain_State;

void Gain_Init(Gain_State *gs);
void Gain_Analysis_Sub(Gain_State *gs, const float *Aq, const float *v,
                       const float *c, float gp, float gc, int sub);

//-----------------------------Bit packing (cl. 4.2.2.7)-----------------------------
#define FRAME_BITS	137			// Encoder output frame size: 137 bits per 30 ms

void Prm_Pack(const uint16_t prm[23], uint8_t bits[FRAME_BITS]);
void Prm_Unpack(const uint8_t bits[FRAME_BITS], uint16_t prm[23]);

//-----------------------------Decoder (cl. 4.2.3)-----------------------------
typedef struct
{
	float    exc_buf[EXC_MEM];	// Decoded excitation, newest samples at the end
	float    lsp_old[10];		// Previous frame's decoded (quantized) LSP
	Gain_State gain;		// Gain prediction state (last quantized energies)
	float    mem_syn[10];		// 1/A(z) synthesis filter memory
	int16_t  old_T0;		// Pitch of the 4th sub-frame of the last frame
	uint16_t old_prm[23];		// Last good frame's parameters (error concealment)
} Decoder_State;

void LSP_Decode(const uint16_t ind[3], const float *lsp_prev, float *lsp);
float Pitch_Adaptive_Sample(const float *exc_buf, const float *v, int n,
                            int16_t T0, int8_t frac_thirds);
void Codebook_Decode_Sub(const float *Aq, int16_t T0, uint16_t code_idx,
                         uint8_t sign, uint8_t shift, float *c);
void Gain_Decode_Sub(Gain_State *gs, const float *Aq, const float *v,
                     const float *c, uint8_t idx, uint8_t bfi, int sub,
                     float *gp_q, float *gc_q);
void ACELP_Decoder_Init(void);
void ACELP_DecodeFrame(const uint8_t *bits, uint8_t bfi, int16_t *synth);

// Openacelp (top-level encode + helpers)
void Filter(int16_t *out, int16_t *inp, float *b, float *a, uint8_t len, int16_t *prev_s, int16_t *prev_w_s);
void Speech_Weighting(int16_t *spch_out, int16_t *spch_in, float a[][11]);
uint8_t Find_Pitch(int16_t *spch, int16_t *prev_s_w);

#endif
