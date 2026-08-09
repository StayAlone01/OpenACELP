// Round-trip codec test: encode a speech-like signal, decode the produced
// 137-bit frames and check the reconstructed speech (SNR, no NaN/inf).
// Also exercises the error-concealment path (BFI = 1) on a fresh decoder.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "openacelp.h"
#include "openacelp_internal.h"

// Frame counter (normally defined in src/main.c, which is not linked here)
uint64_t frame = 0;

#define NF        100                       // Number of frames to encode
#define SIG_LEN   (FRAME_SIZ*NF + LOOK_AHEAD)

static int failures = 0;

#define CHECK(cond, ...) do {                                  \
	if(!(cond)) {                                          \
		failures++;                                    \
		printf("FAIL (line %d): ", __LINE__);          \
		printf(__VA_ARGS__);                           \
		printf("\n");                                  \
	}                                                  \
} while(0)

// Speech-like test signal: alternating 100 ms voiced (harmonic) and unvoiced
// (noise) segments with a slow amplitude modulation.
static void gen_signal(int16_t *sig, int n)
{
	uint32_t seed = 0xC0FFEEu;
	double phase = 0.0;

	for(int i = 0; i < n; i++)
	{
		int seg = (i / 800) % 2;
		double amp = 0.6 + 0.4*sin(2*M_PI*i/(8000.0*0.05));
		double v;

		if(seg == 0)
		{
			phase += 2*M_PI*120.0/8000.0;
			v = amp*(0.6*sin(phase) + 0.3*sin(2.0*phase) + 0.15*sin(3.0*phase));
		}
		else
		{
			seed = seed*1664525u + 1013904223u;
			v = amp*0.5*((double)((seed >> 16) & 0x7FFFu)/16384.0 - 1.0);
		}

		double s = v * 30000.0;
		if(s > 32767.0)
			s = 32767.0;
		if(s < -32768.0)
			s = -32768.0;
		sig[i] = (int16_t)s;
	}
}

int main(void)
{
	int16_t sig[SIG_LEN];
	uint8_t allbits[NF][FRAME_BITS];
	int16_t synth[FRAME_SIZ];

	gen_signal(sig, SIG_LEN);

	// Encode all frames (frame k uses samples 40k .. 40k+279, 40-sample overlap)
	ACELP_Init(grid, w, prev_spch_frame, prev_w_spch_frame);
	for(int k = 0; k < NF; k++)
	{
		frame++;
		ACELP_EncodeFrame(&sig[40*k], allbits[k]);
	}

	// 1) Clean-channel round trip: SNR + non-finite check
	ACELP_Decoder_Init();
	double in_pow = 0.0, err_pow = 0.0;
	int nan_count = 0;
	for(int k = 0; k < NF; k++)
	{
		ACELP_DecodeFrame(allbits[k], 0, synth);
		for(int n = 0; n < FRAME_SIZ; n++)
		{
			double ref = (double)sig[40*k + n];
			double e = (double)synth[n] - ref;
			in_pow += ref*ref;
			err_pow += e*e;
			if(!isfinite((double)synth[n]))
				nan_count++;
		}
	}
	double snr = (err_pow > 1e-12) ? 10.0*log10(in_pow/err_pow) : 100.0;
	printf("clean round trip: %d frames, SNR = %.2f dB, non-finite = %d\n",
	       NF, snr, nan_count);
	CHECK(nan_count == 0, "clean decode produced non-finite samples");
	CHECK(snr > 0.0, "clean decode SNR = %.2f dB (expected > 0)", snr);

	// 2) Error concealment: fresh decoder, BFI set on a few frames
	ACELP_Decoder_Init();
	int cnan = 0;
	for(int k = 0; k < NF; k++)
	{
		uint8_t bfi = (k >= 50 && k < 53) ? 1 : 0;
		ACELP_DecodeFrame(allbits[k], bfi, synth);
		for(int n = 0; n < FRAME_SIZ; n++)
			if(!isfinite((double)synth[n]))
				cnan++;
	}
	printf("concealment run: BFI on frames 50..52, non-finite = %d\n", cnan);
	CHECK(cnan == 0, "BFI concealment produced non-finite samples");

	if(failures == 0)
	{
		printf("All codec round-trip tests passed.\n");
		return 0;
	}
	printf("%d test(s) failed.\n", failures);
	return 1;
}
