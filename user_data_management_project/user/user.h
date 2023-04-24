#ifndef _USER_H
#define _USER_H

#define NUM_THREADS 12
#define NBLOCKS 40
#define REQS 2

#ifdef MULTI_THREAD

#define MULTI_OPS -1
#define SAME_BLOCK_OPS -2

#endif

// For kernel 5.15
#define PUT_DATA 134
#define GET_DATA 156
#define INVALIDATE_DATA 174

// For kernel 4.15
// #define PUT_DATA 134
// #define GET_DATA 174
// #define INVALIDATE_DATA 177

#define TIMES 10
#define SIZE 4096

#define AUDIT if (1)
#endif
