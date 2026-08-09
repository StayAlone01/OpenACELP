// Self-test for the cl. 4.2.2.7 bit packing (src/bits.c)
//
// Verifies:
//   1. the 23 frame fields sum to the 137-bit frame (budget);
//   2. MSB-first placement of every field at its table 3 position;
//   3. a few known absolute bit positions (B49, B130);
//   4. pack -> unpack round trip.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "openacelp_internal.h"

// Independent reference layout (table 3, cl. 4.2.2.7) — intentionally NOT
// taken from src/bits.c, so the test can catch a regression in it.
static const uint8_t W[23] = {
	8, 9, 9,            /* LSP codebooks */
	8, 14, 1, 1, 6,     /* sub-frame 1 */
	5, 14, 1, 1, 6,     /* sub-frame 2 */
	5, 14, 1, 1, 6,     /* sub-frame 3 */
	5, 14, 1, 1, 6      /* sub-frame 4 */
};

static int failures = 0;

#define CHECK(cond, ...) do {                                  \
	if(!(cond)) {                                          \
		failures++;                                    \
		printf("FAIL (line %d): ", __LINE__);          \
		printf(__VA_ARGS__);                           \
		printf("\n");                                  \
	}                                                  \
} while(0)

int main(void)
{
	// 1) Budget: the 23 fields shall sum to 137 bits (cl. 4.2.2.7)
	int total = 0;
	for(int i = 0; i < 23; i++)
		total += W[i];
	CHECK(total == FRAME_BITS, "budget: sum of field widths = %d, expected %d",
	      total, FRAME_BITS);

	// Field start positions (0-indexed frame bit positions; bit 0 = B1)
	int start[23];
	start[0] = 0;
	for(int i = 1; i < 23; i++)
		start[i] = start[i - 1] + W[i - 1];

	// 2) MSB-first placement: a field with only its MSB set must place that
	//    single 1 at the field start, all other bits 0
	uint16_t prm[23], back[23];
	uint8_t bits[FRAME_BITS];
	for(int i = 0; i < 23; i++)
		prm[i] = (uint16_t)(1u << (W[i] - 1));
	Prm_Pack(prm, bits);
	for(int i = 0; i < 23; i++)
	{
		int s = start[i], n = W[i];
		CHECK(bits[s] == 1, "field %d: MSB should sit at bit %d (B%d)", i, s, s + 1);
		for(int b = s + 1; b < s + n; b++)
			CHECK(bits[b] == 0, "field %d: bit %d (B%d) should be 0", i, b, b + 1);
	}

	// 3) Known absolute positions from table 3:
	//    B49 = SF1 global sign (index 48), B130 = SF4 global sign (index 129)
	memset(bits, 0, sizeof(bits));
	memset(prm, 0, sizeof(prm));
	prm[5] = 1;     /* SF1 global sign */
	prm[20] = 1;    /* SF4 global sign */
	Prm_Pack(prm, bits);
	CHECK(bits[48] == 1, "SF1 global sign must be B49 (index 48)");
	CHECK(bits[129] == 1, "SF4 global sign must be B130 (index 129)");

	// 4) Round trip: arbitrary values survive pack -> unpack
	uint32_t seed = 0x12345678u;
	for(int i = 0; i < 23; i++)
	{
		seed = seed * 1664525u + 1013904223u;
		prm[i] = (uint16_t)(seed & ((1u << W[i]) - 1u));
	}
	Prm_Pack(prm, bits);
	Prm_Unpack(bits, back);
	CHECK(memcmp(prm, back, sizeof(prm)) == 0, "pack -> unpack round trip");

	if(failures == 0)
	{
		printf("All bit-packing tests passed (%d bits).\n", FRAME_BITS);
		return 0;
	}
	printf("%d test(s) failed.\n", failures);
	return 1;
}
