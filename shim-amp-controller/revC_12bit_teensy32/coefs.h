/******************************************************************************
 * coefs.h  -  stored shim-current sequence
 *
 * coefStore is a table of currents in amps. Each row has `channels` values,
 * one per channel in the order given by channels_used[][] in the .ino.
 * The rows are grouped into `blocks`: block k is lengths[k] rows long and is
 * repeated reps[k] times; each trigger pulse plays one row.
 *
 * These defaults can be replaced at run time over serial (uploadcoef, or
 * byte 0x01 + header), but uploaded data is lost on power-down.
 *
 * The table here is a single row of zeros. Values not listed are zero, so
 * every row of the sequence outputs 0 A until real data is pasted in or
 * uploaded. lengths/reps are kept at the values the host software expects.
 * (blocks = 9 with only two lengths set is harmless: the sequence wraps
 * after block 2.)
 ******************************************************************************/
#pragma once

const int maxBlocks = 9;                 // size of the lengths/reps arrays
int channels = NUM_B * NUM_C;                      // values per row
int blocks   = 9;                      // number of blocks in use
int lengths[maxBlocks] = {130, 4};    // rows per block
int reps[maxBlocks]    = {10000, 20};    // repeats per block

const long COEF_CAPACITY = 40000;          // max number of stored values
float coefStore[COEF_CAPACITY] = {
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
