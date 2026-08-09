#include "openacelp_internal.h"

//------------------------------------------------------------------
// Bit allocation & multiplexing (EN 300 395-2 cl. 4.2.2.7, table 3)
//------------------------------------------------------------------

// Bit widths of the 23 frame parameters, in table 3 order:
//   LSP split-VQ codebooks (8+9+9) followed by the 4 sub-frames, each with
//   pitch delay, the 14-bit algebraic codebook (pulse 4 .. pulse 1), the
//   pulse global sign, the shift bit and the 6-bit gain VQ index.
static const uint8_t bitno[23] = {
	8, 9, 9,            /* split-VQ LSP codebooks */
	8, 14, 1, 1, 6,     /* sub-frame 1 */
	5, 14, 1, 1, 6,     /* sub-frame 2 */
	5, 14, 1, 1, 6,     /* sub-frame 3 */
	5, 14, 1, 1, 6      /* sub-frame 4 */
};

// Write the nbits LSBs of 'value' into bits[] starting at bit position *pos,
// most significant bit first (table 3: "Bit number (MSB-LSB)").
static void put_bits(uint8_t *bits, int *pos, uint32_t value, int nbits)
{
	for(int i = nbits - 1; i >= 0; i--)
		bits[(*pos)++] = (uint8_t)((value >> i) & 1u);
}

// Read nbits from bits[] starting at bit position *pos, most significant bit
// first. Inverse of put_bits().
static uint32_t get_bits(const uint8_t *bits, int *pos, int nbits)
{
	uint32_t value = 0;
	for(int i = nbits - 1; i >= 0; i--)
		value = (value << 1) | (bits[(*pos)++] & 1u);
	return value;
}

// Pack the 23 encoder parameters into the 137-bit output frame (cl. 4.2.2.7).
// bits[] holds one bit per byte (0/1); frame bit 0 is B1.
void Prm_Pack(const uint16_t prm[23], uint8_t bits[FRAME_BITS])
{
	int pos = 0;
	for(int i = 0; i < 23; i++)
		put_bits(bits, &pos, prm[i], bitno[i]);

#ifdef BIT_DEBUG
	printf("frame=%llu:", (unsigned long long)frame);
	for(int i = 0; i < FRAME_BITS; i++)
	{
		if(i % 8 == 0)
			printf(" ");
		printf("%u", bits[i]);
	}
	printf("\n");
#endif
}

// Unpack the 137-bit frame back into the 23 parameters (decoder entry point,
// cl. 4.2.3). Inverse of Prm_Pack().
void Prm_Unpack(const uint8_t bits[FRAME_BITS], uint16_t prm[23])
{
	int pos = 0;
	for(int i = 0; i < 23; i++)
		prm[i] = (uint16_t)get_bits(bits, &pos, bitno[i]);
}
