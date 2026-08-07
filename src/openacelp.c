#include "openacelp_internal.h"

// Previous frame buffers — defined here, externed in header
int16_t		prev_spch_frame[FRAME_SIZ];			// Previous speech frame
int16_t		prev_w_spch_frame[FRAME_SIZ];		// Previous speech frame (weighted)

// Calculate filtered signal
// Based on input speech and A(z) filter coeff. array
// Arg1: output signal, arg2: input speech
// Arg3: numerator coeffs, arg4: denominator coeffs, arg5: filter length (basically: subframe length)
// Arg6: previous speech frame, arg7: previous speech frame (weighted)
// TODO: I'm not sure, if this filtering works properly. Looks like it does...
void Filter(int16_t *out, int16_t *inp, float *b, float *a, uint8_t len, int16_t *prev_s, int16_t *prev_w_s)
{
	float tmp;
	
	for(uint8_t n=0; n<len; n++)
	{
		tmp=inp[n];
		
		for(uint8_t i=1; i<=10; i++)
		{
			if(n>=i)
				tmp += b[i] * inp[n-i];
			else
				tmp += b[i] * prev_s[FRAME_SIZ+(n-i)];
		}
		for(uint8_t i=1; i<=10; i++)
		{
			if(n>=i)
				tmp += a[i] * out[n-i];
			else
				tmp += a[i] * prev_w_s[FRAME_SIZ+(n-i)];
		}
		
		out[n]=(int16_t)(tmp/11.0);	// Make it fit back into the int16_t
	}
}

// Compute weighted speech for current frame using unquantized LSP params
// For each subframe
// Arg1: output speech, arg2: input speech
// Arg3: array of unquantized LSPs
void Speech_Weighting(int16_t *spch_out, int16_t *spch_in, float a[][11])
{
	// For the intermediate result
	int16_t spch_tmp[FRAME_SIZ];
	
	// Numerator and denominator
	// Of the filter transfer function
	float A_num[11];
	float A_denom[11];
	
	// For each subframe
	for(uint8_t i=0; i<4; i++)
	{
		// Compute numerator and denominator polynomial coeffs
		// For the speech weighting filter
		// Leaving the leading 1.0s alone
		A_num[0]=A_denom[0]=a[i][0];
		
		for(uint8_t j=1; j<=10; j++)
		{
			A_num[j]   = a[i][j] * gamma_3[j-1];
			A_denom[j] = a[i][j] * gamma_4[j-1];
		}

		// Filter the input speech through A_num(z)/A_denom(z)
		if(i==0)
		{
			Filter(&spch_tmp[SUBFRAME_SIZ*i], &spch_in[SUBFRAME_SIZ*i], A_num, A_denom, SUBFRAME_SIZ,
					&prev_spch_frame[FRAME_SIZ-60], &prev_w_spch_frame[FRAME_SIZ-60]);
			if(frame==23)
			{
				for(uint8_t j=0; j<60; j++)
					printf("%d\n", prev_w_spch_frame[FRAME_SIZ-60+j]);
			}
		}
		else
		{
			Filter(&spch_tmp[SUBFRAME_SIZ*i], &spch_in[SUBFRAME_SIZ*i], A_num, A_denom, SUBFRAME_SIZ,
					&spch_in[SUBFRAME_SIZ*(i-1)], &spch_tmp[SUBFRAME_SIZ*(i-1)]);
			if(frame==23)
			{
				for(uint8_t j=0; j<60; j++)
					printf("%d\n", spch_tmp[SUBFRAME_SIZ*(i-1)+j]);
			}
		}
	}
	
	// Test
	/*if(frame==23)
	{
		for(uint8_t i=0; i<11; i++)
		{
			printf("%f\n", A_num[i]);
		}
		printf("\n");
		for(uint8_t i=0; i<11; i++)
		{
			printf("%f\n", A_denom[i]);
		}
	}*/
	
	// Move from buffer to the output
	for(uint8_t i=0; i<FRAME_SIZ; i++)
		spch_out[i]=spch_tmp[i];
}

// Find open loop pitch, once per frame
// Arg1: input weighted speech, arg2: input previous weighted speech
// Arg3: frame length
// Retval: integer T_0 pitch value
uint8_t Find_Pitch(int16_t *spch, int16_t *prev_s_w)
{
	float C[143];
	float max_C[3]={0.0, 0.0, 0.0};	// Values
	uint8_t ind[3]={20, 40, 80};	// Indices
	
	memset(C, 0, 143*sizeof(float));
	
	// First range
	for(uint8_t k=20; k<=39; k++)
	{
		for(uint8_t j=0; j<120; j++)
		{
			if(2*j>=k)
				C[k] += spch[2*j] * spch[2*j-k];
			else
				C[k] += spch[2*j] * prev_s_w[FRAME_SIZ+(2*j-k)];
		}
		if(C[k] > max_C[0])
		{
			max_C[0] = C[k];
			ind[0] = k;
		}
	}
	// Second range
	for(uint8_t k=40; k<=79; k++)
	{
		for(uint8_t j=0; j<120; j++)
		{
			if(2*j>=k)
				C[k] += spch[2*j] * spch[2*j-k];
			else
				C[k] += spch[2*j] * prev_s_w[FRAME_SIZ+(2*j-k)];
		}
		if(C[k] > max_C[1])
		{
			max_C[1] = C[k];
			ind[1] = k;
		}
	}
	// Third range
	for(uint8_t k=80; k<=142; k++)
	{
		for(uint8_t j=0; j<120; j++)
		{
			if(2*j>=k)
				C[k] += spch[2*j] * spch[2*j-k];
			else
				C[k] += spch[2*j] * prev_s_w[FRAME_SIZ+(2*j-k)];
		}
		if(C[k] > max_C[2])
		{
			max_C[2] = C[k];
			ind[2] = k;
		}
	}
	
	// Normalization of C_k maxima — divide by energy of the same
	// 120-sample stride-2 decimated signal used in the correlation numerator
	float norm;
	
	for(uint8_t i=0; i<3; i++)
	{
		norm=0.0;
		
		for(uint8_t j=0; j<120; j++)
		{
			if(2*j >= ind[i])
				norm += spch[2*j-ind[i]] * spch[2*j-ind[i]];
			else
				norm += prev_s_w[FRAME_SIZ+(2*j-ind[i])] * prev_s_w[FRAME_SIZ+(2*j-ind[i])];
		}
		
		if(norm > 0.0f)
			max_C[i] /= sqrtf(norm);
	}
	
	// Find max
	if(max_C[0] > max_C[1] * 0.85)
		return ind[0];
	else if(max_C[1] > max_C[2] * 0.85)
		return ind[1];
	else
		return ind[2];
}

// Encode voice frame
// Arg1: input speech samples, 16-bit signed integer (this frame), arg2: 137 unpacked output bits
void ACELP_EncodeFrame(int16_t *speech, uint8_t *out)
{
	// First call of this function?
	// Make it global later,
	// So it can be accessed outside of this function
	static uint8_t first=1;
	
	// Local buffers for speech frame manipulation
	int16_t spch_in[WINDOW_SIZ];
	int16_t spch_out[WINDOW_SIZ];
	int16_t spch_tmp[FRAME_SIZ];	// Temporary buffer
	
	int32_t		r[11];									// Autocorrelation values
	float		lp[4][11];								// LP coeffs (10, but starting from lp[1], lp[0]=1.0)
	
	// Quantized and unquantized LSP vectors from the previous frame and this one
	static float q_lsp_prev[10];
	float q_lsp_this[10];
	static float lsp_prev[10];
	float lsp_this[10];
	
	// LSP codebook indices for this frame
	uint16_t lsp_cb_indices[3];
	
	// Quantized LSP vector interpolation for 4 subframes
	float q_lsp[4][10];
	
	// Unquantized LSP vector interpolation for 4 subframes
	float lsp[4][10];
	
	// First frame? 
	if(first)
	{
		// Previous quantized LSP vector
		Init_LSP(lsp_prev, q_lsp_prev);
		
		// Set flag to zero
		first = 0;
	}
	
	// Pre processing and windowing
	Speech_Pre_Process(speech, spch_out);
	memcpy(spch_in, spch_out, WINDOW_SIZ*sizeof(int16_t));		// Swap buffers
	memcpy(spch_tmp, spch_out, FRAME_SIZ*sizeof(int16_t));		// Save pre-processed frame for later
	Window_Speech(spch_in, spch_out);
	
	// Compute LSPs for actual frame
	Autocorr(spch_out, r);
	LD_Solver(r, &lp[3][0]);
	LP_LSP(lsp_prev, &lp[3][0], lsp_this);
	
	// Quantize LSPs
	LSP_SVQ(lsp_this, q_lsp_this, lsp_cb_indices);
	
	// Interpolate quantized LSP vector for subframes 4,3,2,1 (indices are 3,2,1,0)
	memcpy(q_lsp[3], q_lsp_this, 10*sizeof(float));
	for(uint8_t i=0; i<10; i++)
	{
		q_lsp[2][i] = 0.75*q_lsp[3][i] + 0.25*q_lsp_prev[i];
		q_lsp[1][i] = 0.50*q_lsp[3][i] + 0.50*q_lsp_prev[i];
		q_lsp[0][i] = 0.25*q_lsp[3][i] + 0.75*q_lsp_prev[i];
	}
	
	// Interpolate unquantized LSP vector for subframes 4,3,2,1 (indices are 3,2,1,0)
	// Computed LSPs are used for subframe 4 (index 3)
	memcpy(&lsp[3], lsp_this, 10*sizeof(float));
	for(uint8_t i=0; i<10; i++)
	{
		lsp[2][i] = 0.75*lsp[3][i] + 0.25*lsp_prev[i];
		lsp[1][i] = 0.50*lsp[3][i] + 0.50*lsp_prev[i];
		lsp[0][i] = 0.25*lsp[3][i] + 0.75*lsp_prev[i];
	}
	
	// Now we have both quantized and unquantized LSP vectors for further computations
	// We can change them back to {a_i} for the A(z)
	
	// Unquantized LSP to A(z) conversion for this frame
	for(uint8_t i=0; i<4; i++)
		LSP_LP(&lsp[i][0], &lp[i][0]);
	
	// "pole-zero type weighting procedure"
	// Calculating weighted speech
	// Input - pre-processed speech
	Speech_Weighting(spch_out, spch_in, lp);
	
	/*if(frame==23)
	{
		printf("----\n");
		for(uint16_t i=0; i<FRAME_SIZ; i++)
			printf("%d\n", spch_in[i]);
	}*/
	
	// Find open loop pitch
	uint8_t T_0 = Find_Pitch(spch_out, prev_w_spch_frame);
	printf("%d\n", T_0);
	
	// Update speech
	memcpy(prev_w_spch_frame, spch_out, FRAME_SIZ*sizeof(int16_t));
	memcpy(prev_spch_frame, spch_tmp, FRAME_SIZ*sizeof(int16_t));
	
	// Update LSPs
	memcpy(q_lsp_prev, q_lsp_this, 10*sizeof(float));
	memcpy(  lsp_prev,   lsp_this, 10*sizeof(float));
}

// Initialize consts
// Arg1: root search grid, arg2: modified Hamming window
// Arg3,4: memory for speech weighting filter
void ACELP_Init(float *search_grid, float *analysis_window, int16_t *f_mem1, int16_t *f_mem2)
{
	Grid_Generate(search_grid);
	Analysis_Window_Init(analysis_window);
	memset(f_mem1, 0, FRAME_SIZ*sizeof(int16_t));
	memset(f_mem2, 0, FRAME_SIZ*sizeof(int16_t));
}
