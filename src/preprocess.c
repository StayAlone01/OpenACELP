#include "openacelp_internal.h"

// Pre-processing: offset compensation filter (cl. 4.2.1)
//   s'(n) = s(n)/2 - s(n-1)/2 + alpha*s'(n-1)
//
// The filter is recursive with alpha ~ 0.999, i.e. it has a very long memory,
// so its state (previous input sample and previous output sample) MUST persist
// across frames. Restarting the state at every frame boundary would inject a
// large step into the pre-processed signal every 30 ms, corrupting the LPC and
// pitch analyses of the whole frame.

static int16_t pre_x0;		// Previous input sample  s(n-1)
static float   pre_y0;		// Previous output sample s'(n-1)

void Pre_Process_Init(void)
{
	pre_x0 = 0;
	pre_y0 = 0.0f;
}

// arg1: present frame, arg2: output
void Speech_Pre_Process(int16_t *inp, int16_t *outp)
{
	#ifdef ERRORS
	if(inp==NULL || outp==NULL)
	{
		printf("\nNULL pointer at Speech_Pre_Process()\n");
		exit(0);
	}
	#endif
	
	for(uint16_t i=0; i<WINDOW_SIZ; i++)
	{
		int16_t x1 = pre_x0;
		pre_x0 = inp[i];
		
		float y = 0.5f*(float)inp[i] - 0.5f*(float)x1 + ALPHA*pre_y0;
		
		// Saturation control (the fixed-point operators saturate on overflow)
		if(y > 32767.0f)
			y = 32767.0f;
		else if(y < -32768.0f)
			y = -32768.0f;
		
		outp[i] = (int16_t)y;
		pre_y0 = y;
	}
	
	// The input buffer holds the present frame (0..FRAME_SIZ-1) followed by
	// the 40-sample look-ahead (FRAME_SIZ..WINDOW_SIZ-1). The look-ahead
	// overlaps the NEXT frame's first 40 samples, so the filter state that
	// must persist across frames is the one AFTER the last sample of the
	// present frame (index FRAME_SIZ-1). Keeping the state from the end of
	// the whole buffer would make the next frame continue from 40 samples in
	// the future and inject a large step at every frame boundary.
	pre_x0 = inp[FRAME_SIZ - 1];
	pre_y0 = (float)outp[FRAME_SIZ - 1];
}
