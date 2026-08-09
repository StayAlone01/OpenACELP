#include <stdio.h>
#include <stdint.h>
#include "openacelp.h"
#include "openacelp_internal.h"

// Frame counter — defined here, externed in header
uint64_t frame = 0;

// Main routine
// Argv[1]: file name (RAW, signed 16-bit, Little-Endian, 8000Hz)
int main(int argc, char *argv[])
{
	FILE *aud;
	
	int16_t spch[WINDOW_SIZ];			// This frame
	
	if(argc==2)
	{
		printf("Loading \"%s\"...\n\n", argv[1]);
		
		aud = fopen(argv[1], "rb");
		
		if(aud==NULL)
		{
			printf("No file named \"%s\"\n", argv[1]);
			printf("Exiting.\n");
			return 1;
		}
		else
		{			
			// Initialize consts etc.
			ACELP_Init(grid, w, prev_spch_frame, prev_w_spch_frame);
			
			// Load 30ms frames, overlapping
			while(fread(spch, 2, WINDOW_SIZ, aud)==WINDOW_SIZ)
			{
				frame++;
				
				// Take us 40 samples back (40 samples * sizeof(int16_t))
				fseek(aud, -80, 1);
				
				//printf("Frame %d\n", frame);
				
				ACELP_EncodeFrame(spch, NULL);
			}
		}
	}
	else
	{
		printf("Invalid params\nExiting.\n");
		return 1;
	}
	
	return 0;
}
