#ifndef __USCTM_H__
#define __USCTM_H__

#define MAX_ACQUIRES 4
#define MAX_FREE 15

extern int get_entries(int *, int*, int, unsigned long *, unsigned long *);
extern void reset_entries(int *, int);
extern void unprotect_memory(void);
extern void protect_memory(void);

extern unsigned long sys_call_table_address;

extern unsigned long sys_ni_syscall_address;

extern int free_entries[MAX_FREE];

extern int num_entries_found;

extern int restore[MAX_ACQUIRES];

#endif