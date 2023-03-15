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

#include "lib/include/scth.h"
#include "file_system/userdatamgmt_fs.h"
#include "userdatamgmt_driver.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(2, _put_data, char *, source, ssize_t, size){
#else
asmlinkage long sys_put_data(char* source, ssize_t size){
#endif


        printk("%s: SYS_PUT_DATA \n",MOD_NAME);


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


        printk("%s: SYS_GET_DATA \n",MOD_NAME);


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


        printk("%s: SYS_INVALIDATE_DATA \n",MOD_NAME);


        return 0;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
long sys_invalidate_data= (unsigned long) __x64_sys_invalidate_data;       
#else
#endif

static ssize_t dev_read(struct file * filp, char __user * buf, size_t len, loff_t * off) {

    struct buffer_head *bh = NULL;
    struct inode * the_inode = filp->f_inode;
    uint64_t file_size = the_inode->i_size;
    int ret;
    loff_t offset;
    int block_to_read;//index of the block to be read from device

    printk("%s: read operation called with len %ld - and offset %lld (the current file size is %lld)",MOD_NAME, len, *off, file_size);

    //this operation is not synchronized 
    //*off can be changed concurrently 
    //add synchronization if you need it for any reason

    //check that *off is within boundaries
    if (*off >= file_size)
        return 0;
    else if (*off + len > file_size)
        len = file_size - *off;

    //determine the block level offset for the operation
    offset = *off % BLK_SIZE; 
    //just read stuff in a single block - residuals will be managed at the applicatin level
    if (offset + len > BLK_SIZE)
        len = BLK_SIZE - offset;

    //compute the actual index of the the block to be read from device
    block_to_read = *off / BLK_SIZE + 2; //the value 2 accounts for superblock and file-inode on device
    
    printk("%s: read operation must access block %d of the device",MOD_NAME, block_to_read);

    bh = (struct buffer_head *)sb_bread(filp->f_path.dentry->d_inode->i_sb, block_to_read);
    if(!bh){
	return -EIO;
    }
    ret = copy_to_user(buf,bh->b_data + offset, len);
    *off += (len - ret);
    brelse(bh);

    return len - ret;

}


static int dev_release(struct inode *inode, struct file *filp) {

    printk ("%s: FS release has been invoked", MOD_NAME);

    return 0;

}

static int dev_open(struct inode *inode, struct file *filp) {

    printk ("%s: FS open has been invoked", MOD_NAME);

    return 0;

}


const struct file_operations dev_fops= {
    .owner = THIS_MODULE,
    .read = dev_read,
    .release = dev_release,
    .open = dev_open,

};
