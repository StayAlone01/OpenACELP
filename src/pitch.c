#include "openacelp_internal.h"

//------------------------------------------------------------------
// Closed-loop long-term prediction analysis
// (EN 300 395-2 cl. 4.2.2.4 - "Long-term prediction analysis")
//
// Implemented from the standard text only. See docs/plan-4.2.2.4.md
// for the design decisions (D1-D3).
//------------------------------------------------------------------

// Fractional-delay interpolation filters. The standard (cl. 4.2.2.4) specifies the
// design only: Hamming-windowed sinc, 8 taps for the correlation interpolation
// (sinc truncated at +/-12, i.e. +/-4 samples at 1/3 resolution) and 32 taps for
// the past excitation interpolation (sinc truncated at +/-48, i.e. +/-16 samples).
//
// 4 phases: -2/3, -1/3, +1/3, +2/3
static float interp_8[4][8];
static float interp_32[4][32];

// Map a fraction given in thirds (-2, -1, +1, +2) to a phase index
static uint8_t frac_phase(int8_t frac_thirds)
{
	switch(frac_thirds)
	{
		case -2: return 0;
		case -1: return 1;
		case  1: return 2;
		case  2: return 3;
	}
	return 1;	// Not reached for valid fractions
}

// Generate a Hamming-windowed sinc interpolation filter for a fractional delay
// 'frac' (in samples, e.g. -2/3). Taps sit at integer sample positions
// i = -taps/2 .. taps/2-1 relative to the integer part of the delay.
static void gen_interp(float frac, float *filt, int taps)
{
	int half = taps / 2;

	for(int i = -half; i < half; i++)
	{
		int j = i + half;							// Window index 0..taps-1
		float w = 0.54f - 0.46f*cos(2.0f*M_PI*(float)j/(float)(taps-1));	// Hamming window
		float x = M_PI*((float)i - frac);			// Sinc argument
		float s = (x == 0.0f) ? 1.0f : sin(x)/x;	// sinc(i - frac)
		filt[i + half] = s*w;
	}
}

// Precompute the two interpolation filter banks
void Pitch_Interp_Init(void)
{
	const float f[4] = { -2.0f/3.0f, -1.0f/3.0f, 1.0f/3.0f, 2.0f/3.0f };

	for(int p = 0; p < 4; p++)
	{
		gen_interp(f[p], interp_8[p], 8);
		gen_interp(f[p], interp_32[p], 32);
	}
}

// Initialise the pitch analysis state. The standard requires the past excitation
// and the past weighted speech samples to be initialised to zero.
void Pitch_Init(Pitch_State *ps)
{
	memset(ps->exc_buf, 0, sizeof(ps->exc_buf));
	memset(ps->w_mem,   0, sizeof(ps->w_mem));
	memset(ps->f_mem,   0, sizeof(ps->f_mem));
	memset(ps->s_mem,   0, sizeof(ps->s_mem));
	ps->T1 = 0;
	memset(ps->pitch_idx, 0, sizeof(ps->pitch_idx));
	memset(ps->gp, 0, sizeof(ps->gp));
	memset(ps->v, 0, sizeof(ps->v));
}

// Denominator coefficients of A(z/0.85): d[i-1] = a[i] * 0.85^i, i = 1..10
static void denom_coeffs(const float *Aq, float *d)
{
	for(int i = 0; i < 10; i++)
		d[i] = Aq[i+1] * gamma_2[i];
}

// Impulse response h[0..59] of the weighted synthesis filter 1/Aq(z/0.85)
static void weighted_impulse(const float *Aq, float *h)
{
	float d[10];
	denom_coeffs(Aq, d);

	h[0] = 1.0f;
	for(int n = 1; n < SUBFRAME_SIZ; n++)
	{
		float acc = 0.0f;
		for(int i = 1; i <= 10 && i <= n; i++)
			acc += d[i-1] * h[n-i];
		h[n] = -acc;
	}
}

// LP residual of one sub-frame: r(n) = s'(n) + sum_i a_i s'(n-i).
// The quantized LP parameters are used (this is also the placeholder excitation,
// plan decision D1). Updates s_mem (last 10 samples of the sub-frame).
static void lp_residual(const int16_t *sprime, const float *Aq, float *s_mem, float *res)
{
	for(int n = 0; n < SUBFRAME_SIZ; n++)
	{
		float acc = (float)sprime[n];
		for(int i = 1; i <= 10; i++)
		{
			int idx = n - i;
			float s = (idx >= 0) ? (float)sprime[idx] : s_mem[10 + idx];
			acc += Aq[i] * s;
		}
		res[n] = acc;
	}
	for(int i = 0; i < 10; i++)
		s_mem[i] = (float)sprime[SUBFRAME_SIZ - 10 + i];
}

// Perceptual weighting of one sub-frame, W(z) = Aq(z)/Aq(z/0.85):
//   sw(n) = res(n) - sum_i d_i sw(n-i)
// Updates w_mem (weighted speech history).
static void perceptual_weight(const float *res, const float *Aq, float *w_mem, float *sw)
{
	float d[10];
	denom_coeffs(Aq, d);

	for(int n = 0; n < SUBFRAME_SIZ; n++)
	{
		float acc = res[n];
		for(int i = 1; i <= 10; i++)
		{
			int idx = n - i;
			float s = (idx >= 0) ? sw[idx] : w_mem[10 + idx];
			acc -= d[i-1] * s;
		}
		sw[n] = acc;
	}
	for(int i = 0; i < 10; i++)
		w_mem[i] = sw[SUBFRAME_SIZ - 10 + i];
}

// Search target: x(n) = sw(n) - zero-input response of the weighted synthesis
// filter 1/Aq(z/0.85), whose memory is f_mem. f_mem is not modified here.
static void target_compute(const float *sw, const float *Aq, const float *f_mem, float *x)
{
	float d[10];
	denom_coeffs(Aq, d);

	float z[SUBFRAME_SIZ];
	for(int n = 0; n < SUBFRAME_SIZ; n++)
	{
		float acc = 0.0f;
		for(int i = 1; i <= 10; i++)
		{
			int idx = n - i;
			float s = (idx >= 0) ? z[idx] : f_mem[10 + idx];
			acc += d[i-1] * s;
		}
		z[n] = -acc;				// Zero input
	}
	for(int n = 0; n < SUBFRAME_SIZ; n++)
		x[n] = sw[n] - z[n];
}

// Filter one sub-frame's error signal (res - u) through 1/Aq(z/0.85) and update
// the weighted-synthesis filter memory f_mem, as cl. 4.2.2.4/4.2.2.6 prescribe:
// the memory is driven by the difference between the LP residual and the actual
// quantized excitation.
static void synth_mem_update(const float *err, const float *Aq, float *f_mem)
{
	float d[10];
	denom_coeffs(Aq, d);

	float out[SUBFRAME_SIZ];
	for(int n = 0; n < SUBFRAME_SIZ; n++)
	{
		float acc = err[n];
		for(int i = 1; i <= 10; i++)
		{
			int idx = n - i;
			float s = (idx >= 0) ? out[idx] : f_mem[10 + idx];
			acc -= d[i-1] * s;
		}
		out[n] = acc;
	}
	for(int i = 0; i < 10; i++)
		f_mem[i] = out[SUBFRAME_SIZ - 10 + i];
}

// Adaptive codebook vector at integer delay T0 plus fraction frac_thirds/3,
// interpolated from the past excitation with the 32-tap filter (frac != 0) or
// taken directly (frac == 0). For delays < 60 the vector extends into the current
// sub-frame, whose excitation is already in the buffer (the LP residual).
static void adaptive_vector(const Pitch_State *ps, int16_t T0, int8_t frac_thirds, float *v)
{
	int base = EXC_MEM - SUBFRAME_SIZ;		// First sample of the current sub-frame

	if(frac_thirds == 0)
	{
		for(int n = 0; n < SUBFRAME_SIZ; n++)
			v[n] = ps->exc_buf[base + n - T0];
	}
	else
	{
		const float *filt = interp_32[frac_phase(frac_thirds)];
		for(int n = 0; n < SUBFRAME_SIZ; n++)
		{
			int idx = base + n - T0;		// Integer part of the delay position
			float acc = 0.0f;
			for(int i = 0; i < 32; i++)
				acc += filt[i] * ps->exc_buf[idx + 16 - i];	// Taps -16..15
			v[n] = acc;
		}
	}
}

// Zero-state response of the weighted synthesis filter to the adaptive vector at
// integer delay k: y(n) = sum_m v(m) * h(n-m)
static void filtered_adaptive(const Pitch_State *ps, int k, const float *h, float *y)
{
	int base = EXC_MEM - SUBFRAME_SIZ;

	for(int n = 0; n < SUBFRAME_SIZ; n++)
	{
		float acc = 0.0f;
		for(int m = 0; m <= n; m++)
			acc += ps->exc_buf[base + m - k] * h[n-m];
		y[n] = acc;
	}
}

// Closed-loop pitch search (cl. 4.2.2.4). Integer pass over [lo, hi], then the four
// fractions -2/3, -1/3, +1/3, +2/3 around the best integer are tested by
// interpolating the normalized correlation with the 8-tap filter (skipped in
// sub-frame 1 when the integer lag is >= 85, per NOTE 3).
static void closed_loop_search(const Pitch_State *ps, const float *x, const float *h,
                               int lo, int hi, int sub,
                               int16_t *T0, int8_t *frac_thirds)
{
	enum { MAXLAG = MAX_PITCH + 8 };
	float C[MAXLAG], E[MAXLAG], rho[MAXLAG];

	int lo_ext = lo - 4;	// A few extra lags outside [lo, hi] so the 8-tap
	int hi_ext = hi + 3;	// fractional interpolation always has neighbours

	// Integer pass over the extended range
	for(int k = lo_ext; k <= hi_ext; k++)
	{
		float y[SUBFRAME_SIZ];
		filtered_adaptive(ps, k, h, y);

		float c = 0.0f, e = 0.0f;
		for(int n = 0; n < SUBFRAME_SIZ; n++)
		{
			c += x[n] * y[n];
			e += y[n] * y[n];
		}
		C[k - lo_ext] = c;
		E[k - lo_ext] = e;
		rho[k - lo_ext] = (e > 1e-9f) ? c / sqrtf(e) : 0.0f;
	}

	// Best integer lag inside [lo, hi], maximizing C^2 / E (eq. 24)
	int best = lo;
	float best_tau = -1e30f;
	for(int k = lo; k <= hi; k++)
	{
		float e = E[k - lo_ext];
		float tau = (e > 1e-9f) ? (C[k - lo_ext]*C[k - lo_ext]) / e : 0.0f;
		if(tau > best_tau)
		{
			best_tau = tau;
			best = k;
		}
	}

	// Fractional refinement around the best integer (interpolated normalized correlation)
	int8_t bfrac = 0;
	float brho = rho[best - lo_ext];

	if(!(sub == 0 && best >= 85))
	{
		static const int8_t fracs[4] = { -2, -1, 1, 2 };
		for(int f = 0; f < 4; f++)
		{
			const float *filt = interp_8[frac_phase(fracs[f])];
			float rf = 0.0f;
			for(int i = 0; i < 8; i++)
			{
				int kk = best + i - 4;		// Taps best-4 .. best+3
				if(kk < lo_ext) kk = lo_ext;
				if(kk > hi_ext) kk = hi_ext;
				rf += filt[i] * rho[kk - lo_ext];
			}
			if(rf > brho)
			{
				brho = rf;
				bfrac = fracs[f];
			}
		}
	}

	*T0 = best;
	*frac_thirds = bfrac;
}

// Zero-state response y = v * h (convolution)
static void convolve_h(const float *v, const float *h, float *y)
{
	for(int n = 0; n < SUBFRAME_SIZ; n++)
	{
		float acc = 0.0f;
		for(int m = 0; m <= n; m++)
			acc += v[m] * h[n-m];
		y[n] = acc;
	}
}

// Pitch gain (eq. 25), clamped to [0, 1.2]
static float pitch_gain(const float *x, const float *y)
{
	float num = 0.0f, den = 0.0f;
	for(int n = 0; n < SUBFRAME_SIZ; n++)
	{
		num += x[n] * y[n];
		den += y[n] * y[n];
	}
	float gp = (den > 1e-9f) ? num / den : 0.0f;
	if(gp < 0.0f) gp = 0.0f;
	if(gp > 1.2f) gp = 1.2f;
	return gp;
}

// Pitch delay coding (plan decision D3).
// Sub-frame 1 (8 bits): idx 0..196   -> 19 1/3 .. 84 2/3 in 1/3 steps,
//                       idx 197..255 -> integers 85..143.
// Sub-frames 2-4 (5 bits): idx 0..31 -> T1 + (idx-17)/3 (offset -17..+14 thirds).
static uint16_t encode_pitch(int16_t T0, int8_t frac_thirds, int sub, int16_t T1)
{
	if(sub == 0)
	{
		int dt = 3*T0 + frac_thirds;			// Delay in thirds
		if(dt <= 254)
			return (uint16_t)(dt - 58);
		else
			return (uint16_t)(197 + (T0 - 85));
	}
	else
	{
		int dt = 3*(T0 - T1) + frac_thirds;		// Offset in thirds, -17..+14
		return (uint16_t)(dt + 17);
	}
}

// Long-term prediction analysis for ONE sub-frame (cl. 4.2.2.4): LP residual,
// perceptually weighted speech, search target, closed-loop pitch search,
// adaptive codebook vector and pitch gain. The caller is responsible for the
// excitation memory update (Excitation_Update, cl. 4.2.2.6) after the gains are
// quantized. T0_ol is the open-loop pitch delay (used by sub-frame 0 only).
// 'res' receives the LP residual of this sub-frame (needed by Excitation_Update
// and by the standard's delay < 60 extension).
void Pitch_Analysis_Sub(Pitch_State *ps, const int16_t *sprime, const float *Aq,
                        int16_t T0_ol, int sub, float *res)
{
	float sw[SUBFRAME_SIZ], x[SUBFRAME_SIZ];
	float h[SUBFRAME_SIZ], y[SUBFRAME_SIZ], v[SUBFRAME_SIZ];
	int16_t T0;
	int8_t frac;

	// LP residual of this sub-frame (also the delay < 60 extension)
	lp_residual(sprime, Aq, ps->s_mem, res);

	// Perceptually weighted speech: W(z) = A(z)/A(z/0.85)
	perceptual_weight(res, Aq, ps->w_mem, sw);

	// Weighted synthesis impulse response: 1/A(z/0.85)
	weighted_impulse(Aq, h);

	// Search target: weighted speech minus zero-input response
	target_compute(sw, Aq, ps->f_mem, x);

	// Append this sub-frame's LP residual to the past-excitation buffer so
	// delays < 60 can be extended by the LP residual (as the standard says).
	// It is overwritten with the true quantized excitation by Excitation_Update.
	memmove(&ps->exc_buf[0], &ps->exc_buf[SUBFRAME_SIZ],
	        (EXC_MEM - SUBFRAME_SIZ) * sizeof(float));
	memcpy(&ps->exc_buf[EXC_MEM - SUBFRAME_SIZ], res, SUBFRAME_SIZ * sizeof(float));

	// Closed-loop pitch search: a limited window around the open-loop pitch
	// (sub-frame 1) or around the first sub-frame's pitch (sub-frames 2-4),
	// bounded by [MIN_PITCH, MAX_PITCH] (cl. 4.2.2.4, NOTE 2)
	int lo, hi;
	if(sub == 0)
	{
		lo = T0_ol - 2;
		if(lo < MIN_PITCH) lo = MIN_PITCH;
		hi = lo + 4;
		if(hi > MAX_PITCH)
		{
			hi = MAX_PITCH;
			lo = hi - 4;
		}
	}
	else
	{
		lo = ps->T1 - 5;
		if(lo < MIN_PITCH) lo = MIN_PITCH;
		hi = lo + 9;
		if(hi > MAX_PITCH)
		{
			hi = MAX_PITCH;
			lo = hi - 9;
		}
	}
	closed_loop_search(ps, x, h, lo, hi, sub, &T0, &frac);

	// Adaptive codebook vector at the chosen delay
	adaptive_vector(ps, T0, frac, v);

	// Filtered adaptive vector and pitch gain (eq. 25)
	convolve_h(v, h, y);
	float gp = pitch_gain(x, y);

	// Store results for later stages (4.2.2.5/4.2.2.6) and the bitstream
	memcpy(ps->v[sub], v, SUBFRAME_SIZ * sizeof(float));
	ps->gp[sub] = gp;
	ps->pitch_idx[sub] = encode_pitch(T0, frac, sub, ps->T1);
	ps->T0[sub] = T0;

	// Innovation target for the codebook search (eq. 26):
	// x2(n) = x(n) - gp*y(n)
	for(int n = 0; n < SUBFRAME_SIZ; n++)
		ps->x2[sub][n] = x[n] - gp*y[n];

	if(sub == 0)
		ps->T1 = T0;

#ifdef PITCH_DEBUG
	printf("frame=%llu sf=%d T0=%d frac=%d gp=%.3f idx=%u\n",
	       (unsigned long long)frame, sub, T0, frac, gp, ps->pitch_idx[sub]);
#endif
}

// Excitation memory update (cl. 4.2.2.6): build the QUANTIZED excitation
//   u(n) = gp_q*v(n) + gc_q*c'(n)
// store it as the new past excitation (replacing the LP residual placeholder,
// plan 4.2.2.4 D1 / 4.2.2.5 D3) and update the weighted-synthesis filter memory
// with the residual error (res - u), as the standard prescribes.
void Excitation_Update(Pitch_State *ps, const float *Aq, const float *res,
                       const float *v, const float *c, float gp_q, float gc_q)
{
	float u[SUBFRAME_SIZ];
	for(int n = 0; n < SUBFRAME_SIZ; n++)
		u[n] = gp_q * v[n] + gc_q * c[n];

	// New past excitation (newest samples at the end of the buffer)
	memcpy(&ps->exc_buf[EXC_MEM - SUBFRAME_SIZ], u, SUBFRAME_SIZ * sizeof(float));

	// Filter memory: filter (res - u) through 1/A(z/0.85); the error between
	// the LP residual and the actual excitation drives the next target.
	float err[SUBFRAME_SIZ];
	for(int n = 0; n < SUBFRAME_SIZ; n++)
		err[n] = res[n] - u[n];
	synth_mem_update(err, Aq, ps->f_mem);
}
