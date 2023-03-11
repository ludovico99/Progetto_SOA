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
#include <linux/module.h>
#include <linux/kernel.h>
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
#include <asm/io.h>
#include <linux/syscalls.h>
#include "lib/include/scth.h"


#include "userdatafs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Francesco Quaglia <francesco.quaglia@uniroma2.it>");
MODULE_DESCRIPTION("USER-DATA-MANAGEMENT-FS");

#define MODNAME "USER-DATA-MANAGEMENT-FS"
unsigned long the_syscall_table = 0x0;
module_param(the_syscall_table, ulong, 0660);


unsigned long the_ni_syscall;

unsigned long new_sys_call_array[] = {0x0, 0x0, 0x0};//please set to sys_put_work at startup
#define HACKED_ENTRIES (int)(sizeof(new_sys_call_array)/sizeof(unsigned long))
int restore[HACKED_ENTRIES] = {[0 ... (HACKED_ENTRIES-1)] -1};


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(2, _put_data, char *, source, ssize_t, size){
#else
asmlinkage long sys_put_data(char* source, ssize_t size){
#endif


        printk("%s: SYS_PUT_DATA \n",MODNAME);


        return 0;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
long sys_put_data = (unsigned long) __x64_sys_put_data;       
#else
#endif


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char *, destination, ssize_t, size){
#else
asmlinkage long sys_get_data(int offset, char* destination, ssize_t size){
#endif


        printk("%s: SYS_GET_DATA \n",MODNAME);


        return 0;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
long sys_get_data = (unsigned long) __x64_sys_get_data;       
#else
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset){
#else
asmlinkage long sys_invalidate_data(int offset){
#endif


        printk("%s: SYS_INVALIDATE_DATA \n",MODNAME);


        return 0;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
long sys_invalidate_data= (unsigned long) __x64_sys_invalidate_data;       
#else
#endif



static struct super_operations my_super_ops = {
};


static struct dentry_operations my_dentry_ops = {
};



int userdatafs_fill_super(struct super_block *sb, void *data, int silent) {   

    struct inode *root_inode;
    struct buffer_head *bh;
    struct userdatafs_sb_info *sb_disk;
    struct timespec64 curr_time;
    uint64_t magic;


    //Unique identifier of the filesystem
    sb->s_magic = MAGIC;

    bh = sb_bread(sb, SB_BLOCK_NUMBER);
    if(!sb){
	return -EIO;
    }
    sb_disk = (struct userdatafs_sb_info *)bh->b_data;
    magic = sb_disk->magic;
    brelse(bh);

    //check on the expected magic number
    if(magic != sb->s_magic){
	return -EBADF;
    }

    sb->s_fs_info = NULL; //FS specific data (the magic number) already reported into the generic superblock
    sb->s_op = &my_super_ops;//set our own operations


    root_inode = iget_locked(sb, 0);//get a root inode indexed with 0 from cache
    if (!root_inode){
        return -ENOMEM;
    }

    root_inode->i_ino = USERDATAFS_ROOT_INODE_NUMBER;//this is actually 10
    inode_init_owner(&init_user_ns, root_inode, NULL, S_IFDIR);//set the root user as owned of the FS root
    root_inode->i_sb = sb;
    root_inode->i_op = &userdatafs_inode_ops;//set our inode operations
    root_inode->i_fop = &userdatafs_dir_operations;//set our file operations
    //update access permission
    root_inode->i_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;

    //baseline alignment of the FS timestamp to the current time
    ktime_get_real_ts64(&curr_time);
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = curr_time;

    // no inode from device is needed - the root of our file system is an in memory object
    root_inode->i_private = NULL;

    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root)
        return -ENOMEM;

    sb->s_root->d_op = &my_dentry_ops;//set our dentry operations

    //unlock the inode to make it usable
    unlock_new_inode(root_inode);

    return 0;
}

static void userdatafs_kill_superblock(struct super_block *s) {
    kill_block_super(s);
    printk(KERN_INFO "%s: userdatafs unmount succesful.\n",MOD_NAME);
    return;
}

//called on file system mounting 
struct dentry *userdatafs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {

    struct dentry *ret;

    ret = mount_bdev(fs_type, flags, dev_name, data, userdatafs_fill_super);

    if (unlikely(IS_ERR(ret)))
        printk("%s: error mounting userdatafs",MOD_NAME);
    else
        printk("%s: userdatafs is succesfully mounted on from device %s\n",MOD_NAME,dev_name);

    return ret;
}

//file system structure
static struct file_system_type userdatafs_type = {
	.owner = THIS_MODULE,
        .name           = "userdatafs",
        .mount          = userdatafs_mount,
        .kill_sb        = userdatafs_kill_superblock,
};


static int userdatafs_init(void) {

    int ret;
    int i = 0;

    
    printk("%s: userdatafs example received sys_call_table address %px\n",MODNAME,(void*)the_syscall_table);
    printk("%s: initializing - hacked entries %d\n",MODNAME,HACKED_ENTRIES);
    

    new_sys_call_array[0] = (unsigned long)sys_put_data;
    new_sys_call_array[1] = (unsigned long)sys_get_data;
    new_sys_call_array[2] = (unsigned long)sys_invalidate_data;

    ret = get_entries(restore,HACKED_ENTRIES,(unsigned long*)the_syscall_table,&the_ni_syscall);


    if (ret != HACKED_ENTRIES){
            printk("%s: could not hack %d entries (just %d)\n",MODNAME,HACKED_ENTRIES,ret);
            return -1;
     }

    unprotect_memory();

    for(i=0;i<HACKED_ENTRIES;i++){
            ((unsigned long *)the_syscall_table)[restore[i]] = (unsigned long)new_sys_call_array[i];
    }

    protect_memory();

    printk("%s: all new system-calls correctly installed on sys-call table\n",MODNAME);

    //register filesystem
    ret = register_filesystem(&userdatafs_type);
    if (likely(ret == 0))
        printk("%s: sucessfully registered userdatafs\n",MOD_NAME);
    else
        printk("%s: failed to register userdatafs - error %d", MOD_NAME,ret);

    return ret;
}

static void userdatafs_exit(void) {

    int ret;
    int i = 0;
    printk("%s: shutting down\n",MODNAME);

    unprotect_memory();
    for(i=0;i<HACKED_ENTRIES;i++){
            ((unsigned long *)the_syscall_table)[restore[i]] = the_ni_syscall;
    }
    protect_memory();
    printk("%s: sys-call table restored to its original content\n",MODNAME);
    //unregister filesystem
    ret = unregister_filesystem(&userdatafs_type);

    if (likely(ret == 0))
        printk("%s: sucessfully unregistered file system driver\n",MOD_NAME);
    else
        printk("%s: failed to unregister userdatafs driver - error %d", MOD_NAME, ret);
}



module_init(userdatafs_init);
module_exit(userdatafs_exit);