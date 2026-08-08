#include "openacelp_internal.h"

//------------------------------------------------------------------
// Algebraic (innovative) codebook: structure and search
// (EN 300 395-2 cl. 4.2.2.5)
//------------------------------------------------------------------

// Fixed pulse amplitudes: +sqrt(2), -1, +1, -1
#define GAIN_I0   1.4142135623730951f		// sqrt(2)

// Focused search parameters (cl. 4.2.2.5)
#define SEARCH_THRESHOLD  0.586f			// k2 = k3
#define SEARCH_MAX_TIME   350				// Worst-case time counter

// Pulse position for index ii and shift bit s:
//   pulse 0 (a=+sqrt2): pos = 2*ii      , ii = 0..29
//   pulse 1 (a=-1)    : pos = 8*ii + 2  , ii = 0..7
//   pulse 2 (a=+1)    : pos = 8*ii + 4  , ii = 0..7   (pos 60 -> not present)
//   pulse 3 (a=-1)    : pos = 8*ii + 6  , ii = 0..7   (pos 62 -> not present)
static inline int pulse_pos(int pulse, int idx, int shift)
{
	switch(pulse)
	{
		case 0: return 2*idx + shift;
		case 1: return 8*idx + 2 + shift;
		case 2: return 8*idx + 4 + shift;
		default: return 8*idx + 6 + shift;
	}
}

// Backward-filtered value at a position. Positions outside the sub-frame
// (>= 60) correspond to pulses that are not present (cl. 4.2.2.5, NOTE 1).
static inline float dval(const float *d, int pos)
{
	return (pos >= 0 && pos < SUBFRAME_SIZ) ? d[pos] : 0.0f;
}

// Exact correlation matrix Phi(i,j) = sum_{n=max(i,j)}^{59} h'(n-i)*h'(n-j),
// i.e. Phi = H^t H of the finite 60x60 convolution matrix (cl. 4.2.2.5, eq. 27).
// Shared buffer, computed once per sub-frame.
static float corr[SUBFRAME_SIZ][SUBFRAME_SIZ];

static void corr_matrix(const float *hprime)
{
	for(int i = 0; i < SUBFRAME_SIZ; i++)
		for(int j = i; j < SUBFRAME_SIZ; j++)
		{
			float acc = 0.0f;
			for(int n = j; n < SUBFRAME_SIZ; n++)
				acc += hprime[n-i] * hprime[n-j];
			corr[i][j] = acc;
			corr[j][i] = acc;
		}
}

// Correlation matrix element; zero if either pulse is not present (>= 60).
static inline float phi(int i, int j)
{
	if(i >= SUBFRAME_SIZ || j >= SUBFRAME_SIZ) return 0.0f;
	return corr[i][j];
}

// Impulse response of the shaping filter A(z/0.75)/A(z/0.85), including the
// fixed-gain pitch contribution (0.8) applied when the integer pitch delay is
// less than the sub-frame size (cl. 4.2.2.5, NOTE 4).
static void shaping_impulse(const float *Aq, int16_t T0, float *F)
{
	float d[10];						// d[i-1] = a[i] * 0.85^i
	for(int i = 0; i < 10; i++)
		d[i] = Aq[i+1] * gamma_2[i];

	for(int n = 0; n < SUBFRAME_SIZ; n++)
	{
		// Numerator A(z/0.75): b[n] = a[n] * 0.75^n, a[0] = 1
		float b = 0.0f;
		if(n == 0)
			b = 1.0f;
		else if(n <= 10)
			b = Aq[n] * gamma_1[n-1];

		float acc = b;
		for(int i = 1; i <= 10 && i <= n; i++)
			acc -= d[i-1] * F[n-i];
		F[n] = acc;
	}

	// Fixed-gain pitch contribution: F(n) += 0.8*F(n-T) for T < 60
	if(T0 < SUBFRAME_SIZ)
	{
		for(int n = T0; n < SUBFRAME_SIZ; n++)
			F[n] += 0.8f * F[n - T0];
	}
}

// Combined impulse response h'(n): F(n) filtered by 1/A(z/0.85)
static void combined_impulse(const float *Aq, const float *F, float *hprime)
{
	float d[10];						// d[i-1] = a[i] * 0.85^i
	for(int i = 0; i < 10; i++)
		d[i] = Aq[i+1] * gamma_2[i];

	for(int n = 0; n < SUBFRAME_SIZ; n++)
	{
		float acc = F[n];
		for(int i = 1; i <= 10 && i <= n; i++)
			acc -= d[i-1] * hprime[n-i];
		hprime[n] = acc;
	}
}

// Backward filtering: d(n) = sum_{i=n}^{59} x2(i) * h'(i-n)   (d = H^t x2)
static void backward_filter(const float *x2, const float *hprime, float *d)
{
	for(int n = 0; n < SUBFRAME_SIZ; n++)
	{
		float acc = 0.0f;
		for(int i = n; i < SUBFRAME_SIZ; i++)
			acc += x2[i] * hprime[i-n];
		d[n] = acc;
	}
}

// Exact maximum absolute 2-pulse and 3-pulse correlations, over both shift
// grids, used for the focused-search thresholds (cl. 4.2.2.5).
static void search_prescan(const float *d, float *max2, float *max3)
{
	float m2 = 0.0f, m3 = 0.0f;

	for(int s = 0; s < 2; s++)
	{
		for(int i0 = 0; i0 < 30; i0++)
		{
			float d0 = dval(d, pulse_pos(0, i0, s));
			for(int i1 = 0; i1 < 8; i1++)
			{
				float c2 = GAIN_I0*d0 - dval(d, pulse_pos(1, i1, s));
				float a2 = fabsf(c2);
				if(a2 > m2) m2 = a2;

				for(int i2 = 0; i2 < 8; i2++)
				{
					float c3 = c2 + dval(d, pulse_pos(2, i2, s));
					float a3 = fabsf(c3);
					if(a3 > m3) m3 = a3;
				}
			}
		}
	}

	*max2 = m2;
	*max3 = m3;
}

// 4-loop algebraic codebook search maximizing C^2 / E (eq. 27-29), with the
// focused-search thresholds and the worst-case time counter (cl. 4.2.2.5).
// Both shift grids are evaluated inside the same loops; the time counter is
// decreased by 3 each time the 4th loop completes and by 4 each time the
// 3rd loop completes, ending the search once it reaches 0.
static void codebook_search(const float *d, float max2, float max3,
                            int *m0, int *m1, int *m2, int *m3, int *shift,
                            float *bestC, float *bestE)
{
	float seuil2 = SEARCH_THRESHOLD * max2;
	float seuil3 = SEARCH_THRESHOLD * max3;
	int time = SEARCH_MAX_TIME;

	int b0 = 0, b1 = 2, b2 = 4, b3 = 6, bs = 0;
	float bc = 0.0f, be = 1.0f;

	for(int i0 = 0; i0 < 30 && time > 0; i0++)
	{
		for(int i1 = 0; i1 < 8 && time > 0; i1++)
		{
			// 2-pulse correlation for both shift grids; enter the 3rd loop
			// only if a threshold is exceeded
			float c2[2];
			c2[0] = GAIN_I0*dval(d, pulse_pos(0, i0, 0)) - dval(d, pulse_pos(1, i1, 0));
			c2[1] = GAIN_I0*dval(d, pulse_pos(0, i0, 1)) - dval(d, pulse_pos(1, i1, 1));
			if(fabsf(c2[0]) <= seuil2 && fabsf(c2[1]) <= seuil2)
				continue;

			for(int i2 = 0; i2 < 8 && time > 0; i2++)
			{
				// 3-pulse correlation for both shift grids
				float c3[2];
				c3[0] = c2[0] + dval(d, pulse_pos(2, i2, 0));
				c3[1] = c2[1] + dval(d, pulse_pos(2, i2, 1));
				if(fabsf(c3[0]) <= seuil3 && fabsf(c3[1]) <= seuil3)
					continue;

				for(int i3 = 0; i3 < 8 && time > 0; i3++)
				{
					for(int s = 0; s < 2; s++)
					{
						int p0 = pulse_pos(0, i0, s);
						int p1 = pulse_pos(1, i1, s);
						int p2 = pulse_pos(2, i2, s);
						int p3 = pulse_pos(3, i3, s);

						// Correlation (eq. 28)
						float c = GAIN_I0*dval(d, p0) - dval(d, p1)
						        + dval(d, p2) - dval(d, p3);

						// Energy (eq. 29)
						float e = 2.0f*phi(p0, p0) + phi(p1, p1)
						        + phi(p2, p2) + phi(p3, p3)
						        - 2.0f*GAIN_I0*phi(p0, p1)
						        + 2.0f*GAIN_I0*phi(p0, p2)
						        - 2.0f*GAIN_I0*phi(p0, p3)
						        - 2.0f*phi(p1, p2)
						        + 2.0f*phi(p1, p3)
						        - 2.0f*phi(p2, p3);

						if(e > 1e-9f && (c*c)/e > (bc*bc)/be)
						{
							bc = c;
							be = e;
							b0 = p0; b1 = p1; b2 = p2; b3 = p3;
							bs = s;
						}
					}
					time -= 3;
				}
				time -= 4;
			}
		}
	}

	*m0 = b0; *m1 = b1; *m2 = b2; *m3 = b3; *shift = bs;
	*bestC = bc; *bestE = be;
}

// Shaped code vector c'(n) and filtered code y2(n) from the chosen pulses:
//   c'(n) = a*F(n-m0) - F(n-m1) + F(n-m2) - F(n-m3)
//   y2(n) = a*h'(n-m0) - h'(n-m1) + h'(n-m2) - h'(n-m3)
// The global sign inverts all pulses simultaneously.
static void build_code(const float *F, const float *hprime,
                       int m0, int m1, int m2, int m3, int sign,
                       float *c, float *y2)
{
	for(int n = 0; n < SUBFRAME_SIZ; n++)
	{
		float cn = 0.0f, yn = 0.0f;
		if(m0 < SUBFRAME_SIZ && n >= m0) { cn += GAIN_I0*F[n-m0];       yn += GAIN_I0*hprime[n-m0]; }
		if(m1 < SUBFRAME_SIZ && n >= m1) { cn -= F[n-m1];               yn -= hprime[n-m1]; }
		if(m2 < SUBFRAME_SIZ && n >= m2) { cn += F[n-m2];               yn += hprime[n-m2]; }
		if(m3 < SUBFRAME_SIZ && n >= m3) { cn -= F[n-m3];               yn -= hprime[n-m3]; }
		if(sign)
		{
			cn = -cn;
			yn = -yn;
		}
		c[n] = cn;
		y2[n] = yn;
	}
}

// Provisional codebook gain (eq. 30): gc = sum x2*y2 / sum y2^2
static float code_gain(const float *x2, const float *y2)
{
	float num = 0.0f, den = 0.0f;
	for(int n = 0; n < SUBFRAME_SIZ; n++)
	{
		num += x2[n] * y2[n];
		den += y2[n] * y2[n];
	}
	float gc = (den > 1e-9f) ? num / den : 0.0f;
	if(gc < 0.0f) gc = 0.0f;
	return gc;
}

// Pack the 14-bit algebraic index (table 4 layout):
//   bits 13..11 = pulse 4, 10..8 = pulse 3, 7..5 = pulse 2, 4..0 = pulse 1.
// The shift bit and the global sign bit are stored separately.
static uint16_t pack_index(int m0, int m1, int m2, int m3)
{
	uint16_t idx = (uint16_t)(m0 >> 1);             // pulse 1
	idx |= (uint16_t)(((m1 - 2) >> 3) << 5);        // pulse 2
	idx |= (uint16_t)(((m2 - 4) >> 3) << 8);        // pulse 3
	idx |= (uint16_t)(((m3 - 6) >> 3) << 11);       // pulse 4
	return idx;
}

void Codebook_Init(Codebook_State *cs)
{
	memset(cs->code_idx, 0, sizeof(cs->code_idx));
	memset(cs->sign, 0, sizeof(cs->sign));
	memset(cs->shift, 0, sizeof(cs->shift));
	memset(cs->gc, 0, sizeof(cs->gc));
	memset(cs->c, 0, sizeof(cs->c));
	memset(cs->y2, 0, sizeof(cs->y2));
}

// Algebraic codebook search for ONE sub-frame (cl. 4.2.2.5): shaping impulse
// response, combined impulse response, backward filtering, correlation matrix,
// focused search, shaped code vector and provisional codebook gain. All the
// results are stored per sub-frame for the later gain quantization (4.2.2.6)
// and the bitstream.
void Codebook_Analysis_Sub(Codebook_State *cs, const Pitch_State *ps,
                           const float *Aq, int sub)
{
	float F[SUBFRAME_SIZ], hprime[SUBFRAME_SIZ];
	float d[SUBFRAME_SIZ];
	const float *x2 = ps->x2[sub];

	// Shaping impulse response (incl. fixed-gain pitch contribution)
	shaping_impulse(Aq, ps->T0[sub], F);

	// Combined impulse response of the shaped weighted synthesis filter
	combined_impulse(Aq, F, hprime);

	// Backward filtered target and correlation matrix of h'
	backward_filter(x2, hprime, d);
	corr_matrix(hprime);

	// Focused-search thresholds (exact maxima)
	float max2, max3;
	search_prescan(d, &max2, &max3);

	// Codebook search
	int m0, m1, m2, m3, shift;
	float bestC, bestE;
	codebook_search(d, max2, max3, &m0, &m1, &m2, &m3, &shift,
	                &bestC, &bestE);

	// Global sign: invert all pulses when the best correlation is negative
	int sign = (bestC < 0.0f) ? 1 : 0;

	// Shaped code vector and filtered code vector
	build_code(F, hprime, m0, m1, m2, m3, sign, cs->c[sub], cs->y2[sub]);

	// Provisional codebook gain (eq. 30); final quantization is cl. 4.2.2.6
	cs->gc[sub] = code_gain(x2, cs->y2[sub]);

	// Store the codebook parameters
	cs->code_idx[sub] = pack_index(m0, m1, m2, m3);
	cs->sign[sub] = (uint8_t)sign;
	cs->shift[sub] = (uint8_t)shift;

#ifdef COD_DEBUG
	float ce = 0.0f;
	for(int n = 0; n < SUBFRAME_SIZ; n++)
		ce += cs->c[sub][n] * cs->c[sub][n];
	printf("frame=%llu sf=%d idx=%u sign=%d shift=%d gc=%.3f c2e=%.3f |c|^2=%.3f\n",
	       (unsigned long long)frame, sub, cs->code_idx[sub], sign, shift,
	       cs->gc[sub], bestE > 1e-9f ? bestC/bestE : 0.0f, ce);
#endif
}
