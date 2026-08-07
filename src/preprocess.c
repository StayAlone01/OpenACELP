#include "openacelp_internal.h"

// Pre Processing: Offset compensation filter (4.2.1)
// y[i] = x[i]/2 - x[i-1]/2 + alpha * y[i-1]
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
	
	outp[0]=inp[0]/2;
	
	for(uint16_t i=1; i<WINDOW_SIZ; i++)
	{
		outp[i] = inp[i]/2 - inp[i-1]/2 + ALPHA*outp[i-1];
	}
}
