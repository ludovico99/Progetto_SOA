#define EXPORT_SYMTAB
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/kprobes.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/apic.h>
#include <linux/syscalls.h>

#include "include/usctm.h"

#define LIBNAME "USCTM"

unsigned long sys_call_table_address = 0x0;
module_param(sys_call_table_address, ulong, 0660);

unsigned long sys_ni_syscall_address = 0x0;
module_param(sys_ni_syscall_address, ulong, 0660);

int free_entries[MAX_FREE];
module_param_array(free_entries,int,NULL,0660);//default array size already known - here we expect to receive what entries are free

int num_entries_found = 0;
module_param(num_entries_found, int, 0660);

int restore[MAX_ACQUIRES] = {[0 ... (MAX_ACQUIRES-1)] -1};

unsigned long cr0;

static inline void write_cr0_forced(unsigned long val)
{
	unsigned long __force_order;

	/* __asm__ __volatile__( */
	asm volatile(
		"mov %0, %%cr0"
		: "+r"(val), "+m"(__force_order));
}

inline void protect_memory(void)
{
	write_cr0_forced(cr0);
}

inline void unprotect_memory(void)
{	
	cr0 = read_cr0();
	write_cr0_forced(cr0 & ~X86_CR0_WP);
}



int get_entries(int num_acquires, unsigned long* sys_call_table, unsigned long *sys_ni_sys_call)
{

	int ret = 0;
	int i = 0;

	int ids[MAX_ACQUIRES] = {[0 ... (MAX_ACQUIRES-1)] -1};
	int entry_ids[MAX_ACQUIRES] = {[0 ... (MAX_ACQUIRES-1)] -1};

	printk("%s: trying to get %d entries from the sys-call table at address %px\n", LIBNAME, num_acquires, (void *)sys_call_table_address);

	if (num_acquires > num_entries_found)
	{
		printk("%s: No more entries available\n", LIBNAME);
		return -1;
	}

	if (num_acquires < 1)
	{
		printk("%s: less than 1 sys-call table entry requested\n", LIBNAME);
		return -1;
	}
	if (num_acquires > MAX_ACQUIRES)
	{
		printk("%s: more than %d sys-call table entries requested\n", LIBNAME, MAX_ACQUIRES);
		return -1;
	}

	for (i = 0; i < num_entries_found && ret < num_acquires; i++)
	{	
		if (restore[i] == -1)
		{	
			printk("%s: acquiring table entry %d\n",LIBNAME,free_entries[i]);
			entry_ids[i] = free_entries[i];
			ids[i] = i;
			ret++;
		}
	}

	if(ret != num_acquires){
		return -1;
	}

	*sys_ni_sys_call = sys_ni_syscall_address;
	*sys_call_table = sys_call_table_address;

	memcpy((char *)restore, (char *)entry_ids, ret * sizeof(int));
	memcpy((char*)indexes, (char*)ids, ret*sizeof(int));

	return ret;
}
