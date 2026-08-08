//------------------------------------------------------------------
// Gain codebook: 2-D vector quantizer for the pitch and innovative
// codebook gains (ETSI EN 300-395-2, cl. 4.2.2.6).
//
//	Codebook			vector dimension	number of vectors
//	    1			  2 - {e_pit, e_cod}		  64 (6 bits)
//
// The entries are log2-energy prediction errors (err_pit, err_cod)
// in the NATURAL float log2 domain used by src/gain.c (no scaling
// offsets, plan decision D2).
//
// The table itself lives in src/gain_codebook.c; regenerate it with
// scripts/gain_codebook_generator.py.
//
// NOTE: the current table is a PLACEHOLDER, generated from synthetic
// speech features. It must be retrained on a real speech corpus
// (TED-LIUM release 1, py-lbg), same as the LSP codebooks.
// See docs/plan-4.2.2.6.md (D1).
//------------------------------------------------------------------

#ifndef GAIN_CODEBOOK
#define GAIN_CODEBOOK

#define GAIN_CB_SIZE	64

extern const float gain_cb[GAIN_CB_SIZE][2];

#endif
