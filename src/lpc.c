#include "openacelp_internal.h"

//analysis window coefficients — defined here, externed in header
float w[WINDOW_SIZ];

//compute the modified Hamming window w(n) coeffs for speech analysis
void Analysis_Window_Init(float *w)
{
	uint16_t L2 = LOOK_AHEAD;		//40 samples look ahead
	uint16_t L1 = FRAME_SIZ;		//240 samples
	
	for(uint16_t i=0; i<L1; i++)
	{
		w[i] = 0.54 - 0.46 * cos((M_PI*i)/((float)L1-1.0));
	}
	for(uint16_t i=L1; i< L1+L2; i++)
	{
		w[i] = 0.54 + 0.46 * cos((M_PI*(i-L1))/((float)L2-1.0));
	}
}

//multiply processed speech samples with modified Hamming window
void Window_Speech(int16_t *inp, int16_t *outp)
{
	for(uint16_t i=0; i<WINDOW_SIZ; i++)
		outp[i] = inp[i] * w[i];
}

//autocorrelation r(k) computation, k=0..10
//additional bandwidth expansion f=60Hz for f_s=8000Hz sample rate
void Autocorr(int16_t *spch, int32_t *r)
{
	uint8_t ovf;
	int64_t tmp;				//for a[0] computation
	uint8_t norm_shift = 0;		//shifts left needed to normalize r[]

	//initially, set r[0]=1 to avoid r[] containing only zeros
	//and zero out the rest
	r[0]=1;
	memset(&r[1], 0, 10*sizeof(int32_t));
	
	//r[0] calculation
	//if r[0] overflows int32_t, divide the signal by 4
	do
	{
		ovf = 0;
		tmp = 0;
		
		for(uint16_t i=0; i<WINDOW_SIZ; i++)
		{
			tmp += (int64_t)spch[i] * (int64_t)spch[i];
		
			if(tmp > (int64_t)INT32_MAX)	//overflow occured?
			{
				//divide the signal by 4
				for(uint16_t j=0; j<WINDOW_SIZ; j++)
					spch[j] /= 4;
				
				ovf = 1;
				
				#ifdef OVF_INFO
				printf("Overflow occured in Autocorr() at i=%d\n", i);
				printf("val=%lld\n", tmp);
				#endif
				
				//break the "for" loop
				break;
			}
		}
	}
	while(ovf);
	
	r[0] = (int32_t)tmp;
	
	//r[0] normalization to the int32_t limit
	//multiply by 2 until we can't no more
	for(uint8_t i=0; i<32; i++)
	{
		while(r[0] < (INT32_MAX/2-1))
		{
			r[0]*=2;
			norm_shift++;
		}
	}
	
	//r[1]..r[10] calculation
	for(uint8_t i=1; i<=10; i++)
	{
		for(uint16_t j=0; j<WINDOW_SIZ; j++)
			r[i] += spch[j] * spch[j-i];
			
		//normalize
		for(uint8_t j=0; j<norm_shift; j++)
			r[i] *= 2;
	}
	
	#ifdef DEBUG
	printf("\nnorm_shift=%d\n", norm_shift);
	for(uint8_t i=0; i<=10; i++)
		printf("r[%d]=%lld\n", i, r[i]);
	printf("\n");
	#endif
	
	//bandwidth expansion, f=60Hz, f_s=8000Hz
	//r[0] is multiplied by 1.00005, which is equivalent to adding a noise floor at -43 dB
	float w_lag[11]={1.0};	//window for bandwidth expansion, w_lag[0]=1.0
	
	for(uint8_t i=1; i<=10; i++)
	{
		w_lag[i] = exp(-0.5 * (2.0 * M_PI * 60.0 * i)/(8000.0));
		w_lag[i] /= 1.00005;
	}
	
	#ifdef DEBUG
	for(uint8_t i=0; i<=10; i++)
		printf("w_lag[i]=%f\n", w_lag[i]);
	printf("\n");
	#endif
	
	//window the autocorrelation values
	for(uint8_t i=0; i<11; i++)
		r[i] *= w_lag[i];
	
	#ifdef DEBUG
	for(uint8_t i=0; i<=10; i++)
		printf("r[%d]=%lld\n", i, r[i]);
	printf("\n");
	#endif
}

//LP coefficients calculation
//based on the Levison-Durbin algorithm
//arg1: modified autocorrelation matrix, arg2: LP filter coeffs
void LD_Solver(int32_t *r, float *a)
{
	//previous frame coeffs - static variable!
	static float prev_a[11] = {1.0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	
	float k, alpha;
	float at[11], an[11];	//t: this iteration, n: next iteration
	float sum;
	
	k = -(float)r[1] / r[0];
	at[1] = k;
	alpha = (float)r[0] * (1.0-k*k);
	
	for(uint8_t i=2; i<=10; i++)
	{
		sum = 0.0;
		
		for(uint8_t j=1; j<=i-1; j++)
		{
			sum += (float)r[j]*at[i-j];
		}
		
		sum += (float)r[i];
		k = -sum / alpha;
		
		//test for filter stability
		//if case of instability, use previous coeffs
		if(fabs(k) > 32750.0/32767.0)	//close enough to 1.0
		{
			memcpy(a, prev_a, 11*sizeof(float));
			#ifdef ERRORS
			printf("Unstable filter, k=%1.2f at i=%d\n", k, i);
			#endif
			return;
		}
		
		//compute new coeffs
		for(uint8_t j=1; j<=i-1; j++)
		{
			an[j] = at[j] + k*at[i-j];
		}
		
		an[i] = k;
		alpha *= (1.0-k*k);
		
		memcpy(at, an, 11*sizeof(float));
	}
	
	prev_a[0]=1.0;
	memcpy(&prev_a[1], &at[1], 10*sizeof(float));
	
	//return solution
	if(a!=NULL)
	{
		a[0]=1.0;	//denominator of the H(z) is A(z)=1+sum(a*z)
		memcpy(&a[1], &at[1], 10*sizeof(float));
	}
	
	#ifdef DEBUG
	for(uint8_t i=0; i<11; i++)
		printf("a[%d]=%f\n", i, a[i]);
	#endif
}
