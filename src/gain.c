#include "openacelp_internal.h"

//------------------------------------------------------------------
// Gain quantization (EN 300 395-2 cl. 4.2.2.6 - "Gain quantization")
//
// Joint 2-D vector quantization of the pitch gain gp and the innovative
// codebook gain gc in the log2-energy domain, 6 bits per sub-frame.
//
// Implemented from the standard text only. The D1-D5 design decisions
// are summarised below; see docs/codebook_training.md for the training:
//   D1 - gain codebook trained by us (include/gain_codebook.h)
//   D2 - energies in the natural float log2 domain (no scaling offsets)
//   D3 - quantized energies limited to 27 dB (pitch) / 25 dB (code), as
//        required by the standard (cl. 4.2.2.6)
//   D4 - prediction state initialised to 0
//   D5 - interleaved per sub-frame (see ACELP_EncodeFrame)
//------------------------------------------------------------------

// Small guard so log2 never sees a zero argument (D2: the codebook is
// trained in the same convention, so this does not shift the operating point).
#define LOG_GUARD	1e-12f

// Energy of the impulse response of 1/A(z) over one sub-frame: E_lpc
// (h(0) = 1, 60 samples).
static float lpc_impulse_energy(const float *Aq)
{
	float h[SUBFRAME_SIZ];
	h[0] = 1.0f;
	for(int n = 1; n < SUBFRAME_SIZ; n++)
	{
		float acc = 0.0f;
		for(int i = 1; i <= 10 && i <= n; i++)
			acc += Aq[i] * h[n-i];
		h[n] = -acc;
	}

	float e = 0.0f;
	for(int n = 0; n < SUBFRAME_SIZ; n++)
		e += h[n] * h[n];
	return e;
}

// Sum of squares of a sub-frame vector (E_p / E_c).
static float vector_energy(const float *x)
{
	float e = 0.0f;
	for(int n = 0; n < SUBFRAME_SIZ; n++)
		e += x[n] * x[n];
	return e;
}

// log2 of a squared gain, guarded against zero.
static float log2_square(float g)
{
	float s = g * g;
	return log2f(s > LOG_GUARD ? s : LOG_GUARD);
}

// Energy prediction from the last quantized energies:
//   pred = 0.5*last_pit + 0.25*last_cod - 3.0, clamped at 0.
static float gain_predict(float last_pit, float last_cod)
{
	float p = 0.5f*last_pit + 0.25f*last_cod - 3.0f;
	return (p > 0.0f) ? p : 0.0f;
}

// 2-D codebook search: minimize (err_pit - cb[k][0])^2 + (err_cod - cb[k][1])^2
// over the 64 entries (6 bits).
static uint8_t gain_search(float err_pit, float err_cod)
{
	uint8_t best = 0;
	float best_d = -1.0f;

	for(int k = 0; k < GAIN_CB_SIZE; k++)
	{
		float d0 = err_pit - gain_cb[k][0];
		float d1 = err_cod - gain_cb[k][1];
		float d = d0*d0 + d1*d1;
		if(best_d < 0.0f || d < best_d)
		{
			best_d = d;
			best = (uint8_t)k;
		}
	}
	return best;
}

// Quantized pitch gain: 2^(0.5*(last_pit - e_p)), clamped to 1.2.
static float gain_pit_q(float last_pit, float e_p)
{
	float g = exp2f(0.5f*(last_pit - e_p));
	if(g > 1.2f)
		g = 1.2f;
	return g;
}

// Quantized code gain: 2^(0.5*(last_cod - e_c)). No clamp on the gain
// itself (the standard only limits the *energy* to 25 dB, above).
static float gain_cod_q(float last_cod, float e_c)
{
	return exp2f(0.5f*(last_cod - e_c));
}

void Gain_Init(Gain_State *gs)
{
	gs->last_ener_pit = 0.0f;
	gs->last_ener_cod = 0.0f;
	memset(gs->gain_idx, 0, sizeof(gs->gain_idx));
	memset(gs->gp, 0, sizeof(gs->gp));
	memset(gs->gc, 0, sizeof(gs->gc));
}

// Gain quantization for ONE sub-frame (cl. 4.2.2.6).
//   Aq  - quantized LP coefficients of this sub-frame
//   v   - adaptive codebook vector
//   c   - shaped innovation code vector
//   gp  - unquantized (provisional) pitch gain (eq. 25)
//   gc  - unquantized (provisional) code gain (eq. 30)
//   sub - sub-frame index (0..3), for storing the results
void Gain_Analysis_Sub(Gain_State *gs, const float *Aq, const float *v,
                       const float *c, float gp, float gc, int sub)
{
	// Energies in the natural float log2 domain (D2)
	float e_p = log2f(vector_energy(v) * lpc_impulse_energy(Aq) + LOG_GUARD);
	float e_c = log2f(vector_energy(c) * lpc_impulse_energy(Aq) + LOG_GUARD);

	// Contribution energies of the pitch and code parts
	float ener_pit = log2_square(gp) + e_p;
	float ener_cod = log2_square(gc) + e_c;

#ifdef GAIN_TRAINING
	// Training mode (scripts/gain_codebook_generator.py): the codebook is
	// not used; the prediction state is driven by the UNQUANTIZED energies
	// so the error vectors (err_pit, err_cod) can be collected for LBG.
	// Gains pass through unquantized so the encoder keeps running.
	float pred_pit = gain_predict(gs->last_ener_pit, gs->last_ener_cod);
	float pred_cod = gain_predict(gs->last_ener_cod, gs->last_ener_pit);
	fprintf(stderr, "%f %f\n", ener_pit - pred_pit, ener_cod - pred_cod);
	gs->last_ener_pit = ener_pit;
	gs->last_ener_cod = ener_cod;
	gs->gp[sub] = gp;
	gs->gc[sub] = gc;
	gs->gain_idx[sub] = 0;
#else
	// Prediction from the last QUANTIZED energies (cross-coupled)
	float pred_pit = gain_predict(gs->last_ener_pit, gs->last_ener_cod);
	float pred_cod = gain_predict(gs->last_ener_cod, gs->last_ener_pit);

	// Prediction errors
	float err_pit = ener_pit - pred_pit;
	float err_cod = ener_cod - pred_cod;

	// 2-D vector quantization (6 bits)
	uint8_t idx = gain_search(err_pit, err_cod);
	gs->gain_idx[sub] = idx;

	// Quantized energies become the prediction state of the next sub-frame
	gs->last_ener_pit = gain_cb[idx][0] + pred_pit;
	gs->last_ener_cod = gain_cb[idx][1] + pred_cod;

	// Limit the quantized energies to 27 dB (pitch) / 25 dB (code),
	// cl. 4.2.2.6: "These quantized energies ... are limited respectively to
	// 27 and 25 in order to avoid bursts of energy in case of non-recovered
	// transmission errors." The future decoder's Dec_Ener must apply the same clamps.
	if(gs->last_ener_pit > 27.0f) gs->last_ener_pit = 27.0f;
	if(gs->last_ener_cod > 25.0f) gs->last_ener_cod = 25.0f;

	// Quantized gains
	gs->gp[sub] = gain_pit_q(gs->last_ener_pit, e_p);
	gs->gc[sub] = gain_cod_q(gs->last_ener_cod, e_c);

#ifdef GAIN_DEBUG
	// Debug hook (scripts/validate_gains.py). Printed to stderr like the
	// LSP_DEBUG hook in src/lsp.c, so capturing both with 2>&1 cannot
	// interleave them mid-line (stdout is block-buffered when piped and
	// would otherwise split the lines against the unbuffered stderr).
	fprintf(stderr, "frame=%llu sf=%d idx=%u gp_q=%.3f gc_q=%.3f gp=%.3f gc=%.3f e_p=%.2f e_c=%.2f err=(%.2f,%.2f)\n",
	       (unsigned long long)frame, sub, idx, gs->gp[sub], gs->gc[sub],
	       gp, gc, e_p, e_c, err_pit, err_cod);
#endif
#endif
}

// Decode the quantized pitch and code gains for ONE sub-frame (cl. 4.2.3.1.4),
// mirror of Gain_Analysis_Sub. Computes the vector energies, the prediction
// and the quantized energies from the 6-bit VQ index, then the gains. On a bad
// frame (bfi) the prediction energies of the previous sub-frame are reduced by
// 0.5 (1.5 dB) and no codebook is used (cl. 4.2.3.2).
void Gain_Decode_Sub(Gain_State *gs, const float *Aq, const float *v,
                     const float *c, uint8_t idx, uint8_t bfi, int sub,
                     float *gp_q, float *gc_q)
{
	// Energies in the natural float log2 domain (same convention as the encoder)
	float e_p = log2f(vector_energy(v) * lpc_impulse_energy(Aq) + LOG_GUARD);
	float e_c = log2f(vector_energy(c) * lpc_impulse_energy(Aq) + LOG_GUARD);

	if(bfi)
	{
		// Error concealment: decrease the energies of the previous sub-frame
		// by 1.5 dB (cl. 4.2.3.2)
		gs->last_ener_pit -= 0.5f;
		gs->last_ener_cod -= 0.5f;
		if(gs->last_ener_pit < 0.0f) gs->last_ener_pit = 0.0f;
		if(gs->last_ener_cod < 0.0f) gs->last_ener_cod = 0.0f;
	}
	else
	{
		// Prediction from the last quantized energies (cross-coupled)
		float pred_pit = gain_predict(gs->last_ener_pit, gs->last_ener_cod);
		float pred_cod = gain_predict(gs->last_ener_cod, gs->last_ener_pit);

		// Quantized energies from the 6-bit VQ index
		gs->last_ener_pit = gain_cb[idx][0] + pred_pit;
		gs->last_ener_cod = gain_cb[idx][1] + pred_cod;

		// Limit the quantized energies (cl. 4.2.2.6)
		if(gs->last_ener_pit > 27.0f) gs->last_ener_pit = 27.0f;
		if(gs->last_ener_cod > 25.0f) gs->last_ener_cod = 25.0f;
	}

	*gp_q = gain_pit_q(gs->last_ener_pit, e_p);
	*gc_q = gain_cod_q(gs->last_ener_cod, e_c);

	gs->gain_idx[sub] = idx;
	gs->gp[sub] = *gp_q;
	gs->gc[sub] = *gc_q;
}
