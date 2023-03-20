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

// #include <linux/compiler.h>
// #include <linux/percpu.h>
// #include <asm/percpu.h>
// #include <linux/blk_types.h>
// #include <linux/blkdev.h>

#include "lib/include/scth.h"
#include "file_system/userdatamgmt_fs.h"
#include "userdatamgmt_driver.h"

struct blk_rcu_tree **the_tree;

int max_open_manageable = 2;
module_param(max_open_manageable, int, 0660);

struct bdev_metadata bdev_md = {NULL, 0, NULL};
DEFINE_PER_CPU(int, id);

DEFINE_PER_CPU(loff_t, my_off);

static __always_inline loff_t *get_off(void)
{
    return this_cpu_ptr(&my_off); // legge la variabile per-CPU
}

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
    // int offset = 0;
    // int i = 0;
    // struct blk_element* elem = NULL;
    printk("%s: SYS_INVALIDATE_DATA \n", MOD_NAME);
    //  for (i = 0; i < NBLOCKS; i++)
    // {
    //     offset = get_offset(i);
    //     // bh = (struct buffer_head *)sb_bread(s, offset);
    //     // if (!bh)
    //     // {
    //     //     printk("%s: Error in retrieving the block at offset %d (index %d) in the device", MOD_NAME, offset, i);
    //     //     __sync_fetch_and_sub(bdev_usage_ptr, 1);
    //     //     return -EIO;
    //     // }

    //     AUDIT printk("%s: Flushing block at offset %d (index %d) into the device", MOD_NAME, offset, i);
    //     elem = lookup(the_tree[*index]->head, get_index(block_to_read));
    //     if (elem->dirtiness)
    //         mark_buffer_dirty(bh);
    //     if (sync_dirty_buffer(bh) == 0)
    //     {
    //         AUDIT printk("%s: SUCCESS IN SYNCHRONOUS WRITE", MODNAME);
    //     }
    //     else
    //         printk("%s: FAILURE IN SYNCHRONOUS WRITE", MODNAME);
    //     brelse(bh);
    // }


    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
long sys_invalidate_data = (unsigned long)__x64_sys_invalidate_data;
#else
#endif

static ssize_t dev_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    loff_t *my_off_ptr = get_off();
    int *index = this_cpu_ptr(&id);
    struct buffer_head *bh = NULL;
    struct blk *blk = NULL;
    struct inode *the_inode = filp->f_inode;
    uint64_t file_size = the_inode->i_size;
    struct super_block *sb = filp->f_path.dentry->d_inode->i_sb;
    int ret = 0;
    loff_t offset;
    int block_to_read; // index of the block to be read from device

    //AUDIT printk("%s: read operation called with len %ld - and offset %lld (the current file size is %lld)", MOD_NAME, len, *off, file_size);

    *my_off_ptr = *off; // In this way each CPU has its own private copy and *off can't be changed concurrently

    // check that *off is within boundaries
    if (*my_off_ptr >= file_size)
    {
        return 0;
    }
    else if (*my_off_ptr + len > file_size)
        len = file_size - *my_off_ptr;

    // determine the block level offset for the operation
    offset = *my_off_ptr % BLK_SIZE;
    // just read stuff in a single block - residuals will be managed at the application level
    if (offset + len > BLK_SIZE)
        len = BLK_SIZE - offset;

    // compute the actual index of the the block to be read from device
    block_to_read = *my_off_ptr / BLK_SIZE + 2; // the value 2 accounts for superblock and file-inode on device

    //AUDIT printk("%s: read operation must access block %d of the device", MOD_NAME, block_to_read);

    // bh = (struct buffer_head *)sb_bread(sb, block_to_read);
    // if (!bh)
    // {
    //     AUDIT printk("%s: Error in retrieving the block %d", MOD_NAME, block_to_read);
    //     return -EIO;
    // }
    // blk = (struct blk *)bh->b_data;
    blk = lookup(the_tree[*index]->head, get_index(block_to_read))->blk;
   // AUDIT printk("%s: The block %p  %d", MOD_NAME, blk, get_validity(blk->metadata));
    if (blk != NULL && get_validity(blk->metadata))
    {
        //AUDIT printk("%s: The block at offset %d is valid", MOD_NAME, block_to_read);
        ret = copy_to_user(buf, blk + offset, len);
        *my_off_ptr += (len - ret);
    }
    else
        *my_off_ptr += BLK_SIZE;

    //brelse(bh);
    *off = *my_off_ptr;
    return len - ret;
}

static int dev_release(struct inode *inode, struct file *filp)
{
    
    int *index = this_cpu_ptr(&id);
    AUDIT printk("%s: Device release has been invoked: the thread %d trying to release the device file\n ", MOD_NAME, current->pid);
    if (bdev_md.bdev == NULL)
    {
        printk("%s: Consistency check: the device is not mounted", MOD_NAME);
        return -ENODEV;
    }

    AUDIT printk("%s: Freeing the struct allocated in the kernel memory", MOD_NAME);
    __sync_fetch_and_sub(&(bdev_md.bdev_usage), 1);
   //AUDIT printk("%s: bdev_usage is %d",MOD_NAME, bdev_md.bdev_usage);
    free_structs(the_tree[*index]);
    the_tree[*index] = NULL;
    return 0;
}

static int dev_open(struct inode *inode, struct file *filp)
{
    struct super_block *sb = filp->f_path.dentry->d_inode->i_sb;
    const char *dev_name = filp->f_path.dentry->d_iname;
    struct blk_element *blk_elem = NULL;
    struct buffer_head *bh = NULL;
    int offset = 0;
    int i = 0;
    int temp = 0;
    int *index = this_cpu_ptr(&id);

    AUDIT printk("%s: Device open has been invoked: the thread %d trying to open file\n ", MOD_NAME, current->pid);

    // initiliazation of the RCU tree
    // Start filling the block device representation in RAM
    // bdev_md = (struct bdev_metadata *)kzalloc(sizeof(struct bdev_metadata), GFP_KERNEL);
    // if (!bdev_md)
    // {
    //     printk("%s: Error allocationg bdev_metadata struct\n", MOD_NAME);
    //     return -ENOMEM;
    // }
    // bdev_md -> bdev_usage = 0;
    // if (bdev_md->bdev == NULL)
    // {
    //     kfree(bdev_md);
    //     printk("%s: Unable to get the struct block_device for %s", MOD_NAME, dev_name);
    //     return -EINVAL;
    // }

    bdev_md.bdev = sb->s_bdev;
    bdev_md.path = dev_name;
    if (the_tree == NULL)
    {
        the_tree = (struct blk_rcu_tree **)kzalloc(sizeof(struct blk_rcu_tree *) * max_open_manageable, GFP_KERNEL);
        if (!the_tree)
        {
            printk("%s: Error allocationg the tree\n", MOD_NAME);
            return -ENOMEM;
        }
    }
    for (temp = 0; temp < max_open_manageable; temp++){
        if (the_tree[temp] == NULL){
            *index = temp;
            break;
        }
    }
    if (temp == max_open_manageable) return - ENOMEM;
    
    AUDIT printk("%s: INDICE TROVATO %d",MOD_NAME, *index);
    the_tree[*index] = (struct blk_rcu_tree *)kzalloc(sizeof(struct blk_rcu_tree), GFP_KERNEL);
    if (!the_tree[*index])
    {
        printk("%s: Error allocationg blk_rcu_tree struct\n", MOD_NAME);
        return -ENOMEM;
    }
    rcu_tree_init(the_tree[*index]);
    // Initialization of struct blk_metadata
    for (i = 0; i < NBLOCKS; i++)
    {

        offset = get_offset(i);
        bh = (struct buffer_head *)sb_bread(sb, offset);
        if (!bh)
        {
            printk("%s: Error retrieving the block at offset %d\n", MOD_NAME, offset);
            return -EIO;
        }
        if (bh->b_data != NULL)
        {
            //AUDIT printk("%s: retrieved the block at offset %d (index %d)\n", MOD_NAME, offset, i);
            blk_elem = (struct blk_element *)kzalloc(sizeof(struct blk_element), GFP_KERNEL);

            if (!blk_elem)
            {
                printk("%s: Error allocationg blk_element struct\n", MOD_NAME);
                return -ENOMEM;
            }

            blk_elem->blk = (struct blk *)bh->b_data;
            blk_elem->index = i;

            //AUDIT printk("%s: Block at offset %d (index %d) is %d (1 stays for valid) contains the message = %s\n", MOD_NAME, offset, i, get_validity(blk_elem->blk->metadata), blk_elem->blk->data);

            insert(&the_tree[*index]->head, blk_elem);
        }
        brelse(bh);
    }
    if (filp->f_mode & FMODE_WRITE)
    {
        printk("%s: Write operation not allowed", MOD_NAME);
        return -EROFS;
    }
    // Avoiding that a concurrent thread may have killed the sb with unomunt operation
    __sync_fetch_and_add(&(bdev_md.bdev_usage), 1);
    return 0;
}

const struct file_operations dev_fops = {
    .owner = THIS_MODULE,
    .read = dev_read,
    .release = dev_release,
    .open = dev_open,

};
