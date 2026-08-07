#include "openacelp_internal.h"

// LSP search grid — defined here, externed in header
// GRID_SIZ+1 entries (0..GRID_SIZ): the scan in LP_LSP() reads grid[GRID_SIZ] as the
// w=pi endpoint.
float grid[GRID_SIZ+1];

// Generate grid of values for LSP computation
void Grid_Generate(float *g)
{
	// g[i] = cos(pi*i/GRID_SIZ), i = 0..GRID_SIZ -> q spans 1.0 (w=0) down to -1.0 (w=pi)
	// in GRID_SIZ equal steps: the "60 points equally spaced between 0 and pi" of
	// [1] cl. 4.2.2.2.
	g[0] = 1.0;			// cos(0)
	g[GRID_SIZ] = -1.0;	// cos(pi)
	
	for(uint8_t i=1; i<GRID_SIZ; i++)
		g[i] = cos((M_PI*i)/GRID_SIZ);
}

// LSP F(z) polynomial evaluation using Chebyshev polynomials
// Arg1: input value, arg2: f() coeffs
// Retval: evaluated value
float Chebyshev_Eval(float x, float *f)
{
	uint8_t n=5;	// Coeffs num
	
	float b0, b1, b2;
	
	b2 = f[0];
	b1 = 2.0*x + f[1];
	
	for(uint8_t i=2; i<n; i++)
	{
		b0 = 2.0*x*b1 - b2 + f[i];
		b2 = b1;
		b1 = b0;
	}
	
	return x*b1 - b2 + 0.5*f[n];
}

// Convert LP to LSP
// Arg1: previous frame LSP array, arg2: present frame LP array, arg3: output LSP array
void LP_LSP(float *prev_LSP, float *a, float *LSP)
{
	float f1[6] = {1.0, 0, 0, 0, 0, 0};
	float f2[6] = {1.0, 0, 0, 0, 0, 0};
	float *coefs;							// Coeff set that we are using
	uint8_t found=0;						// Found roots
	uint8_t loc=0;							// Location in the grid
	float x1, x2, y1, y2, xm, ym, x, y;		// Vals for root search

	// 5 polynomial coeffs
	for(uint8_t i=0; i<5; i++)
 	{
		// The 10 here means the 10th order LP filter, so the 10th order LSP polynomial
		f1[i+1] = a[i+1] + a[10-i] - f1[i];
		f2[i+1] = a[i+1] - a[10-i] + f2[i];
	}
	
	#ifdef DEBUG
	// Evaluation of the polynomial - root search
	printf("\n");
	for(uint8_t i=0; i<GRID_SIZ; i++)
		;//printf("%f\n", Chebyshev_Eval(grid[i], f2));
	#endif
	
	// Look for roots in f1 first
	coefs = f1;
	
	// Search init
	x1 = grid[0];
	y1 = Chebyshev_Eval(x1, coefs);
	
	// Search for the roots
	// Until we have 10 or we have searched thru all the grid values (0..pi)
	while(found<10 && loc<GRID_SIZ)
	{
		loc++;	// Move thru the grid
		
		x2 = x1;
		y2 = y1;
		x1 = grid[loc];
		y1 = Chebyshev_Eval(x1, coefs);
		
		// Check for a sign change
		if(y1*y2 <= 0)
		{
			// Divide the range 4 times
			for(uint8_t i=0; i<4; i++)
      		{
        		xm = 0.5 * (x1+x2);
        		ym = Chebyshev_Eval(xm, coefs);
  				
  				// Sign change in the lower half?
				if(y1*ym <= 0)
				{
					// Move there
						y2 = ym;
						x2 = xm;
				}
				// Same thing here - zero crossing in the second half?
				else
				{
					// Move there
						y1 = ym;
						x1 = xm;
				}
			}
        	
			// Linear interpolation for the fine root value
			// (Original code commented out: the fabs() dropped the sign of (y2-y1),
			//  so whenever y1 > 0 the estimate extrapolated OUTSIDE the bracket [x1,x2].
			/*
			x = x2-x1;
			y = y2-y1;
        	
			if(fabs(y)<0.0001)	// Unsafe to compare floats to 0.0
				x = x1;
			else
			{
				y = (x2-x1)/(y2-y1);
				y=fabs(y);
				x1 = x1 - y1*y;
			}
			*/
        	
			// Correct linear interpolation:
			//   x_root = x1 - y1*(x2-x1)/(y2-y1)
			// No fabs(): y1*y2 <= 0 guarantees the root lies inside [x1,x2].
			y = y2-y1;
			if(fabs(y) >= 0.0001)	// Avoid division by (near) zero
				x1 = x1 - y1*(x2-x1)/(y2-y1);
			// else: keep the refined bisection result in x1
			
			LSP[found]=x1;
			found++;
			
			#ifdef DEBUG
			printf("%f|%f|%d|%f\n", x1, y2, loc, 8000.0/(2*M_PI)*acos(x1));
			#endif
			
			// Swap f1 with f2 and vice-versa, for next search
			if(coefs == f1)
			{
				coefs = f2;
			}
			else
			{
				coefs = f1;
  	    	}
		}
        	
		// Apply new value of y1
		y1 = Chebyshev_Eval(x1, coefs);
	}
	
	// Check if we have found all 10 roots
	// If not - copy old roots and use them
	if(found<10 && prev_LSP!=NULL)
	{
		memcpy(LSP, prev_LSP, 10*sizeof(float));
		#ifdef DEBUG
		printf("\nLess than 10 roots found in LP_LSP()\n");
		#endif
	}
}

// Split vector quantization of LSP parameters
// Full codebook search with squared error metric (saving one division)
// Arg1: LSPs in cosine domain (10), arg2: quantized LSPs output (10), arg3: codebook indices output (3)
void LSP_SVQ(float *lsp, float *q_lsp, uint16_t *ind)
{
	uint16_t ind_rv[3];
	
	float se;
	float delta;
	float min=10000.0;
	
	// Codebook 1 search
	for(uint16_t i=0; i<size_cb1; i++)
	{
		se=0.0;
		
		for(uint8_t j=0; j<3; j++)
		{
			delta = lsp[j]-cb1[i*3+j];
			se += delta*delta;
		}
		
		if(se < min)
		{
			min = se;
			ind_rv[0]=i;
		}
	}
	    
	min=10000.0;
	
	// Codebook 2 search
	for(uint16_t i=0; i<size_cb2; i++)
	{		
		se=0.0;
		
		for(uint8_t j=0; j<3; j++)
		{
			delta = lsp[3+j]-cb2[i*3+j];
			se += delta*delta;
		}
		
		if(se < min)
		{
			min = se;
			ind_rv[1]=i;
		}
	}
	  
	min=10000.0;
	
	// Codebook 3 search
	for(uint16_t i=0; i<size_cb3; i++)
	{
		se=0.0;
		
		for(uint8_t j=0; j<4; j++)
		{
			delta = lsp[6+j]-cb3[i*4+j];
			se += delta*delta;
		}
		
		if(se < min)
		{
			min = se;
			ind_rv[2]=i;
		}
	}
	
	// Return quantized vector...
	memcpy(&q_lsp[0], &cb1[ind_rv[0]], 3*sizeof(float));
	memcpy(&q_lsp[3], &cb2[ind_rv[1]], 3*sizeof(float));
	memcpy(&q_lsp[6], &cb3[ind_rv[2]], 4*sizeof(float));
	
	// ...and codebook indices
	memcpy(ind, ind_rv, 3*sizeof(uint16_t));
}

// Convert LSP coeffs to F1(z) or F2(z)
// Arg1: LSP array of length 10, arg2: F_1(z) or F_2(z) coefficients output
void LSP_Poly(float *lsp, float *f)
{
	uint8_t k=0;
	
	f[0] = 1.0;
	f[1] = -2.0 * lsp[k];
	k+=2;
	
	for(uint8_t i=2; i<=5; i++)
	{
		f[i] = -2.0*lsp[k]*f[i-1]+2.0*f[i-2];
		
		for(int8_t j=i-1; j>=1; j--)
		{
			if(j>1)
				f[j] = f[j] - 2.0*lsp[k]*f[j-1] + f[j-2];
			else
				f[j] = f[j] - 2.0*lsp[k]*f[j-1];	// F(-1)=0
		}
		
		k+=2;
	}
}

// Convert LSP to LP (A(z))
// Arg1: input LSP array, arg2: computed LP array
void LSP_LP(float *lsp, float *a)
{
	float f1[6], f2[6];
	
	// Get F1(z) and F2(z) coeffs
	LSP_Poly(&lsp[0], f1);
	LSP_Poly(&lsp[1], f2);
	
	for(int8_t i=5; i>0; i--)
	{
		f1[i] += f1[i-1];
		f2[i] -= f2[i-1];
	}
	
	a[0]=1.0;
	
	int8_t i, j;
	for(i=1, j=10; i<=5; i++, j--)
	{
		a[i] = 0.5 * (f1[i] + f2[i]);
		a[j] = 0.5 * (f1[i] - f2[i]);
	}
}

// Initialize "previous" frame LSP vectors
// Arg1: array of LSP unquantized vectors
// Arg2: array of quantized LSP vectors
// Order of args doesnt matter
void Init_LSP(float *in1, float *in2)
{
	in1[0] = in2[0] = 30000.0/32768.0;
	in1[1] = in2[1] = 26000.0/32768.0;
	in1[2] = in2[2] = 21000.0/32768.0;
	in1[3] = in2[3] = 15000.0/32768.0;
	in1[4] = in2[4] = 8000.0/32768.0;
	in1[5] = in2[5] = 0.0;
	in1[6] = in2[6] = -8000.0/32768.0;
	in1[7] = in2[7] = -15000.0/32768.0;
	in1[8] = in2[8] = -21000.0/32768.0;
	in1[9] = in2[9] = -26000.0/32768.0;
}
