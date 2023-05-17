#ifndef _USER_H
#define _USER_H

#define NUM_THREADS 4 // Number of threads to be spawned
#define NBLOCKS 10000   // Number of blocks of the image
#define REQS 10       // Number of system call invocation for each thread
#define TO_READ 150   // Number of bytes to be read by dev_read and sys_get_data
#ifdef MULTI_THREAD

#define MULTI_OPS -1
#define SAME_BLOCK_OPS -2

#endif

#define DEV_READ -3
// For kernel 5.15
#define PUT_DATA 134        // Syscall number for sys_put_data
#define GET_DATA 156        // Syscall number for sys_get_data
#define INVALIDATE_DATA 174 // Syscall number for sys_invalidate_data

// For kernel 4.15
// #define PUT_DATA 134
// #define GET_DATA 174
// #define INVALIDATE_DATA 177

#define BLK_SIZE 4096 // Block size

#define AUDIT if (1)
#endif
