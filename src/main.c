#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "openacelp.h"
#include "openacelp_internal.h"

// Frame counter — defined here, externed in header
uint64_t frame = 0;

// Main routine
// Argv[1]: file name (RAW, signed 16-bit, Little-Endian, 8000Hz)
// Encode a RAW speech file (signed 16-bit, little-endian, 8 kHz).
// With a second file argument, also write the 137-bit frames (unpacked, one
// bit per byte) for later decoding.
static void encode_file(const char *in_name, const char *bits_name)
{
	FILE *aud = fopen(in_name, "rb");
	FILE *fout = bits_name ? fopen(bits_name, "wb") : NULL;

	if(!aud)
	{
		printf("No file named \"%s\"\n", in_name);
		printf("Exiting.\n");
		return;
	}

	printf("Loading \"%s\"...\n\n", in_name);

	// Initialize consts etc.
	ACELP_Init(grid, w, prev_spch_frame, prev_w_spch_frame);

	int16_t spch[WINDOW_SIZ];
	uint8_t bits[FRAME_BITS];

	// Load 30ms frames, overlapping
	while(fread(spch, 2, WINDOW_SIZ, aud) == WINDOW_SIZ)
	{
		frame++;

		// Take us 40 samples back (40 samples * sizeof(int16_t))
		fseek(aud, -80, 1);

		ACELP_EncodeFrame(spch, bits);
		if(fout)
			fwrite(bits, 1, FRAME_BITS, fout);
	}

	if(fout)
		fclose(fout);
	fclose(aud);
}

// Decode a file of 137-bit frames (unpacked) into a RAW speech file.
// BFI is 0 on this clean-channel path.
static void decode_file(const char *bits_name, const char *out_name)
{
	FILE *fin = fopen(bits_name, "rb");
	FILE *fout = fopen(out_name, "wb");

	if(!fin || !fout)
	{
		printf("Cannot open \"%s\" or \"%s\"\n", bits_name, out_name);
		printf("Exiting.\n");
		return;
	}

	// Initialize consts etc.
	ACELP_Init(grid, w, prev_spch_frame, prev_w_spch_frame);
	ACELP_Decoder_Init();

	uint8_t bits[FRAME_BITS];
	int16_t synth[FRAME_SIZ];

	while(fread(bits, 1, FRAME_BITS, fin) == FRAME_BITS)
	{
		frame++;
		ACELP_DecodeFrame(bits, 0, synth);
		fwrite(synth, 2, FRAME_SIZ, fout);
	}

	fclose(fin);
	fclose(fout);
}

// Main routine
//   openacelp <input.raw>              encode only
//   openacelp <input.raw> <bits.bin>   encode + save 137-bit frames
//   openacelp -d <bits.bin> <out.raw>  decode (clean channel, BFI = 0)
int main(int argc, char *argv[])
{
	if(argc == 2)
	{
		encode_file(argv[1], NULL);
		return 0;
	}
	if(argc == 3)
	{
		encode_file(argv[1], argv[2]);
		return 0;
	}
	if(argc == 4 && strcmp(argv[1], "-d") == 0)
	{
		decode_file(argv[2], argv[3]);
		return 0;
	}

	printf("Invalid params\n");
	printf("Usage:\n");
	printf("  %s <input.raw>                  encode only\n", argv[0]);
	printf("  %s <input.raw> <bits.bin>       encode + save 137-bit frames\n", argv[0]);
	printf("  %s -d <bits.bin> <out.raw>      decode (BFI = 0)\n", argv[0]);
	return 1;
}
