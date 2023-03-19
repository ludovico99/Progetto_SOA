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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
__SYSCALL_DEFINEx(2, _put_data, char *, source, ssize_t, size)
{
#else
asmlinkage long sys_put_data(char *source, ssize_t size)
{
#endif

    printk("%s: SYS_PUT_DATA \n", MOD_NAME);

    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
long sys_put_data = (unsigned long)__x64_sys_put_data;
#else
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char *, destination, ssize_t, size)
{
#else
asmlinkage long sys_get_data(int offset, char *destination, ssize_t size)
{
#endif
    // unsigned long * epoch = &(the_tree->epoch);
    // unsigned long my_epoch;

    //  if (the_tree == NULL){
    //     printk("%s: Consistency check on the_tree struct", MODNAME);
    //     return -ENODEV;
    // }
    // my_epoch = __sync_fetch_and_add(epoch,1)

    // index = (my_epoch & MASK) ? 1 : 0; //get_index(my_epoch);
    // __sync_fetch_and_add(&the_tree->standing[index],1);

    printk("%s: SYS_GET_DATA \n", MOD_NAME);

    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
long sys_get_data = (unsigned long)__x64_sys_get_data;
#else
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset)
{
#else
asmlinkage long sys_invalidate_data(int offset)
{
#endif

    printk("%s: SYS_INVALIDATE_DATA \n", MOD_NAME);

    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
long sys_invalidate_data = (unsigned long)__x64_sys_invalidate_data;
#else
#endif

static ssize_t dev_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{

    struct buffer_head *bh = NULL;
    struct blk *blk = NULL;
    unsigned int *bdev_usage_ptr =  &(the_tree.bdev_md -> bdev_usage);
    struct inode *the_inode = filp->f_inode;
    uint64_t file_size = the_inode->i_size;
    int ret = 0;
    loff_t offset;
    int block_to_read; // index of the block to be read from device

    printk("%s: read operation called with len %ld - and offset %lld (the current file size is %lld)", MOD_NAME, len, *off, file_size);

    // this operation is not synchronized
    //*off can be changed concurrently
    // add synchronization if you need it for any reason

    // Avoiding that a concurrent thread may have killed the sb
    __sync_fetch_and_add(bdev_usage_ptr,1);

    // check that *off is within boundaries
    if (*off >= file_size){
        __sync_fetch_and_sub(bdev_usage_ptr,1);
        return 0;
    }
    else if (*off + len > file_size)
        len = file_size - *off;

    // determine the block level offset for the operation
    offset = *off % BLK_SIZE;
    // just read stuff in a single block - residuals will be managed at the application level
    if (offset + len > BLK_SIZE)
        len = BLK_SIZE - offset;

    // compute the actual index of the the block to be read from device
    block_to_read = *off / BLK_SIZE + 2; // the value 2 accounts for superblock and file-inode on device

    AUDIT printk("%s: read operation must access block %d of the device", MOD_NAME, block_to_read);

    bh = (struct buffer_head *)sb_bread(filp->f_path.dentry->d_inode->i_sb, block_to_read);
    if (!bh)
    {   AUDIT printk("%s: error in retrieving the block %d", MOD_NAME);
        __sync_fetch_and_sub(bdev_usage_ptr,1);
        return -EIO;
    }
    blk = (struct blk *)bh->b_data;
    AUDIT printk("%s: The block %p  %d", MOD_NAME, blk, get_validity(blk->metadata));
    if (blk != NULL && get_validity(blk->metadata))
    {
        AUDIT printk("%s: The block at offset %d is valid", MOD_NAME, block_to_read);
        ret = copy_to_user(buf, bh->b_data + offset, len);
        *off += (len - ret);
    }
    else
        *off += BLOCK_SIZE;

    brelse(bh);
    __sync_fetch_and_sub(bdev_usage_ptr,1);
    return len - ret;
}

static int dev_release(struct inode *inode, struct file *filp)
{

    AUDIT printk("%s: Device release has been invoked: the thread %d trying to open file\n ", MOD_NAME, current->pid);

    if ((&the_tree)->bdev_md == NULL || (&the_tree)->bdev_md->bdev == NULL)
    {
        printk("%s: Consistency check: the device is not mounted", MOD_NAME);
        return -ENODEV;
    }

    return 0;
}

static int dev_open(struct inode *inode, struct file *filp)
{

    AUDIT printk("%s: Device open has been invoked: the thread %d trying to open file\n ", MOD_NAME, current->pid);
    if ((&the_tree)->bdev_md == NULL || (&the_tree)->bdev_md->bdev == NULL)
    {
        printk("%s: Consistency check: the device is not mounted", MOD_NAME);
        return -ENODEV;
    }

    if (filp->f_mode & FMODE_WRITE)
    {
        printk("%s: Write operation not allowed", MOD_NAME);
        return -EROFS;
    }

    return 0;
}

const struct file_operations dev_fops = {
    .owner = THIS_MODULE,
    .read = dev_read,
    .release = dev_release,
    .open = dev_open,

};
