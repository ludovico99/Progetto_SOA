#define EXPORT_SYMTAB
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/timekeeping.h>
#include <linux/time.h>
#include <linux/buffer_head.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/kprobes.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <asm/apic.h>
#include <asm/io.h>
#include <linux/syscalls.h>

#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/namei.h>

#include "file_system/userdatamgmt_fs.h"
#include "userdatamgmt_driver.h"
#include "utils.h"
#include "userdatamgmt.h"
#include "lib/include/scth.h"

#include "userdatamgmt_sc.c"
#include "file_system/userdatamgmt_fs_src.c"
#include "userdatamgmt_driver.c"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ludovico Zarrelli");
MODULE_DESCRIPTION("BLOCK-LEVEL DATA MANAGEMENT SERVICE");

unsigned long the_syscall_table = 0x0;
module_param(the_syscall_table, ulong, 0660);

char mount_pt[255]  = "/";
module_param_string(mount_point, mount_pt, 255, 0660);

unsigned long the_ni_syscall;

unsigned long new_sys_call_array[] = {0x0, 0x0, 0x0};
#define HACKED_ENTRIES (int)(sizeof(new_sys_call_array)/sizeof(unsigned long))
int restore[HACKED_ENTRIES] = {[0 ... (HACKED_ENTRIES-1)] -1};


int init_module(void) {

    int ret;
    int i = 0;

    
    printk("%s: dev example received sys_call_table address %px\n",MOD_NAME,(void*)the_syscall_table);
    printk("%s: initializing - hacked entries %d\n",MOD_NAME,HACKED_ENTRIES);
    

    new_sys_call_array[0] = (unsigned long)sys_put_data;
    new_sys_call_array[1] = (unsigned long)sys_get_data;
    new_sys_call_array[2] = (unsigned long)sys_invalidate_data;

    ret = get_entries(restore,HACKED_ENTRIES,(unsigned long*)the_syscall_table,&the_ni_syscall);


    if (ret != HACKED_ENTRIES){
            printk("%s: could not hack %d entries (just %d)\n",MOD_NAME,HACKED_ENTRIES,ret);
            return -1;
     }

    unprotect_memory();

    for(i=0;i<HACKED_ENTRIES;i++){
            ((unsigned long *)the_syscall_table)[restore[i]] = (unsigned long)new_sys_call_array[i];
    }

    protect_memory();

    printk("%s: all new system-calls correctly installed on sys-call table\n",MOD_NAME);

    //register filesystem
    ret = register_filesystem(&userdatafs_type);
    if (likely(ret == 0))
        printk("%s: sucessfully registered userdatafs\n",MOD_NAME);
    else
        printk("%s: failed to register userdatafs - error %d", MOD_NAME,ret);

    return 0;
}

void cleanup_module(void) {

    int ret;
    int i = 0;
    printk("%s: shutting down\n",MOD_NAME);

    unprotect_memory();
    for(i=0;i<HACKED_ENTRIES;i++){
            ((unsigned long *)the_syscall_table)[restore[i]] = the_ni_syscall;
    }
    protect_memory();
    printk("%s: sys-call table restored to its original content\n",MOD_NAME);

    //unregister filesystem
    ret = unregister_filesystem(&userdatafs_type);

    if (likely(ret == 0))
        printk("%s: sucessfully unregistered file system driver\n",MOD_NAME);
    else
        printk("%s: failed to unregister driver - error %d", MOD_NAME, ret);
}


