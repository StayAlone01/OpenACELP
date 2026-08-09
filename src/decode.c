#include "openacelp_internal.h"

//------------------------------------------------------------------
// Speech decoder (EN 300 395-2 cl. 4.2.3)
//------------------------------------------------------------------

// Decoder state (defined in include/openacelp_internal.h)
static Decoder_State dec_st;

// Floor division by 3 (correct for negative values). Used to split a delay
// expressed in thirds into an integer lag and a fraction.
static int div3_floor(int x)
{
	int q = x / 3;
	if(x < 0 && (x % 3) != 0)
		q--;
	return q;
}

// Pitch index decoding (cl. 4.2.3.1.2), inverse of the encoder's index mapping
// (src/pitch.c, encode_pitch). Returns the integer lag; *frac receives the
// fraction in thirds (0, -1 or -2, all supported by the interpolation
// filters). The fractional delay T0 + frac/3 is recovered exactly:
//   sub-frame 1, idx < 197 : delay in thirds = idx + 58   (19 1/3 .. 84 2/3)
//   sub-frame 1, idx >= 197: integer lag     = idx - 112 (85 .. 143)
//   sub-frames 2-4         : delay relative to T1 = idx - 17 thirds
static int16_t decode_pitch(uint16_t idx, int sub, int16_t T1, int8_t *frac)
{
	if(sub == 0)
	{
		if(idx < 197)
		{
			int dt = (int)idx + 58;			// Delay in thirds
			int T0 = div3_floor(dt + 2);
			*frac = (int8_t)(dt - 3*T0);
			return (int16_t)T0;
		}
		*frac = 0;
		return (int16_t)((int)idx - 112);
	}
	else
	{
		int dt = (int)idx - 17;				// Relative delay in thirds from T1
		int d0 = div3_floor(dt + 2);
		int T0 = T1 + d0;
		*frac = (int8_t)(dt - 3*d0);
		return (int16_t)T0;
	}
}

// Short-term synthesis filter: s(n) = u(n) - sum_{i=1..10} a_i s(n-i)
// (cl. 4.2.3.1.5, eq. 41), with the filter memory updated to the last 10
// output samples.
static void synth_filter(const float *Aq, const float *u, float *s, float *mem)
{
	for(int n = 0; n < SUBFRAME_SIZ; n++)
	{
		float acc = u[n];
		for(int i = 1; i <= 10; i++)
			acc -= Aq[i] * ((n >= i) ? s[n-i] : mem[10 - i + n]);
		s[n] = acc;
	}
	memcpy(mem, &s[SUBFRAME_SIZ - 10], 10*sizeof(float));
}

// Post-processing: multiply the reconstructed speech by 2 with saturation
// control (cl. 4.2.1), producing 16-bit signed samples.
static void post_process(const float s[4][SUBFRAME_SIZ], int16_t *out)
{
	for(int sub = 0; sub < 4; sub++)
		for(int n = 0; n < SUBFRAME_SIZ; n++)
		{
			float v = 2.0f * s[sub][n];
			if(v > 32767.0f)
				v = 32767.0f;
			else if(v < -32768.0f)
				v = -32768.0f;
			out[sub*SUBFRAME_SIZ + n] = (int16_t)v;
		}
}

// Initialize the decoder state: zero excitation and filter memories, previous
// LSPs at the standard initial values, gain prediction reset, old_T0 = 60.
void ACELP_Decoder_Init(void)
{
	memset(&dec_st, 0, sizeof(dec_st));
	Gain_Init(&dec_st.gain);
	Init_LSP(dec_st.lsp_old, dec_st.lsp_old);
	dec_st.old_T0 = 60;
}

// Decode one 30 ms frame (cl. 4.2.3).
//   bits  - 137 unpacked bits (one per byte, as produced by ACELP_EncodeFrame)
//   bfi   - bad frame indicator (1 = frame lost/corrupted -> error concealment)
//   synth - 240 reconstructed speech samples (16-bit signed)
void ACELP_DecodeFrame(const uint8_t *bits, uint8_t bfi, int16_t *synth)
{
	uint16_t prm[23];
	Prm_Unpack(bits, prm);

	// Quantized LSP vector of this frame (cl. 4.2.3.1.1)
	float lsp_this[10];
	if(bfi == 0)
	{
		LSP_Decode(&prm[0], dec_st.lsp_old, lsp_this);
		memcpy(dec_st.old_prm, prm, sizeof(prm));
	}
	else
	{
		// Error concealment: keep the previous LSPs and reuse the last good
		// frame's parameters (cl. 4.2.3.2)
		memcpy(lsp_this, dec_st.lsp_old, 10*sizeof(float));
		memcpy(prm, dec_st.old_prm, sizeof(prm));
	}

	// Interpolate the LSPs per sub-frame and convert to A(z) (eq. 22)
	float lsp_int[4][10], Aq[4][11];
	Int_LSP(dec_st.lsp_old, lsp_this, lsp_int);
	for(int sub = 0; sub < NUM_PITCH_SUB; sub++)
		LSP_LP(&lsp_int[sub][0], &Aq[sub][0]);

	memcpy(dec_st.lsp_old, lsp_this, 10*sizeof(float));

	int16_t T1 = 0;					// Integer part of the sub-frame 1 pitch lag
	int16_t T0;
	float sub_synth[4][SUBFRAME_SIZ];

	for(int sub = 0; sub < NUM_PITCH_SUB; sub++)
	{
		const float *A = Aq[sub];

		// Shift the excitation history: the current sub-frame sits at the end
		memmove(&dec_st.exc_buf[0], &dec_st.exc_buf[SUBFRAME_SIZ],
		        (EXC_MEM - SUBFRAME_SIZ) * sizeof(float));

		// Pitch index decoding (cl. 4.2.3.1.2); on a bad frame the previous
		// frame's 4th sub-frame pitch is repeated with zero fraction
		int8_t frac;
		if(bfi)
		{
			T0 = dec_st.old_T0;
			frac = 0;
		}
		else if(sub == 0)
		{
			T0 = decode_pitch(prm[3], 0, 0, &frac);
		}
		else
		{
			T0 = decode_pitch(prm[3 + 5*sub], sub, T1, &frac);
		}

		// Shaped innovative vector (cl. 4.2.3.1.3)
		float c[SUBFRAME_SIZ];
		Codebook_Decode_Sub(A, T0, prm[4 + 5*sub], (uint8_t)prm[5 + 5*sub],
		                    (uint8_t)prm[6 + 5*sub], c);

		// Adaptive codebook vector; the excitation repeats itself for delays
		// below the sub-frame length (cl. 4.2.2.4)
		float v[SUBFRAME_SIZ];
		for(int n = 0; n < SUBFRAME_SIZ; n++)
			v[n] = Pitch_Adaptive_Sample(dec_st.exc_buf, v, n, T0, frac);

		// Decoded gains (cl. 4.2.3.1.4)
		float gp_q, gc_q;
		Gain_Decode_Sub(&dec_st.gain, A, v, c, (uint8_t)prm[7 + 5*sub], bfi,
		                sub, &gp_q, &gc_q);

		// Excitation u(n) = gp*v(n) + gc*c'(n) (eq. 40); store it as the new
		// past excitation
		float u[SUBFRAME_SIZ];
		for(int n = 0; n < SUBFRAME_SIZ; n++)
		{
			u[n] = gp_q * v[n] + gc_q * c[n];
			dec_st.exc_buf[EXC_MEM - SUBFRAME_SIZ + n] = u[n];
		}

		// Synthesis 1/A(z) (eq. 41)
		synth_filter(A, u, sub_synth[sub], dec_st.mem_syn);

		if(sub == 0)
			T1 = T0;
	}

	// Post-processing: x2 with saturation (cl. 4.2.1)
	post_process(sub_synth, synth);

	// Remember the 4th sub-frame pitch for the error concealment
	dec_st.old_T0 = T0;
}
