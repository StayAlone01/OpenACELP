//------------------------------------------------------------------
// LSP codebooks: split vector quantization with 3 codebooks.
// The LSPs are in the cosine domain [-1, 1]. Refer to
// ETSI EN 300-395-2, chapter 4.2.2.3.
//
// Codebook		  vector dimension		number of vectors
//     1		3 -   {q1, q2, q3}		  256 (8 bits)
//     2		3 -   {q4, q5, q6}		  512 (9 bits)
//     3		4 - {q7, q8, q9, q10}	  512 (9 bits)
//
// The tables themselves live in src/lsp_codebook.c; regenerate them
// with scripts/lsp_codebook_generator.py (LBG) from encoder
// -DLSP_TRAINING output.
//------------------------------------------------------------------

#ifndef LSP_CODEBOOKS
#define LSP_CODEBOOKS

// Dimensions
#define dim_cb1		3
#define dim_cb2		3
#define dim_cb3		4
// Sizes
#define size_cb1	256
#define size_cb2	512
#define size_cb3	512

extern const float cb1[dim_cb1*size_cb1];
extern const float cb2[dim_cb2*size_cb2];
extern const float cb3[dim_cb3*size_cb3];

#endif
