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

struct blk_rcu_tree the_tree;

DEFINE_PER_CPU(loff_t, my_off);

static __always_inline loff_t *get_off(void)
{
    return this_cpu_ptr(&my_off); // legge la variabile per-CPU
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
__SYSCALL_DEFINEx(2, _put_data, char *, source, ssize_t, size)
{
#else
asmlinkage int sys_put_data(char *source, ssize_t size)
{
#endif
    struct blk_element *the_block;
    struct dev_blk* the_dev_blk;
    struct buffer_head * bh;
    char * destination;
    uint16_t *the_metadata = NULL;
    unsigned long residual_bytes = -1;
    int ret = -1;

    printk("%s: SYS_PUT_DATA \n", MOD_NAME);

     if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        return -ENODEV;
    }

    if (size > SIZE)
        size = SIZE;
    if (size < 0)
        return -EINVAL;

    __sync_fetch_and_add(&(bdev_md.open_count),1);
    spin_lock(&(the_tree.write_lock));

    the_block = inorderTraversal(the_tree.head);

    if (the_block == NULL) {
        printk("%s: No blocks available in the device\n",MOD_NAME);
        ret = -ENOMEM;
        goto exit_2;
    }

    destination = (char*) kzalloc(BLK_SIZE, GFP_KERNEL); 

    if (!destination){
    
        printk("%s: Kzalloc has failed\n",MOD_NAME);
        ret = -ENOMEM;
        goto exit_2;
   }
    residual_bytes = copy_from_user(destination + MD_SIZE, source, size);
    AUDIT printk("%s: Copy_from_user residual bytes %ld", MOD_NAME, residual_bytes);

    ret = get_offset(the_block->index);
    the_metadata = &(the_block -> metadata);

    AUDIT printk("%s: Old metadata for block at index %d (index %d) are %x", MOD_NAME, ret, get_index(ret), *the_metadata);

    the_block ->dirtiness = 1;
    asm volatile("mfence");
    *the_metadata = set_valid(*the_metadata);
    *the_metadata = set_not_free(*the_metadata); 
    *the_metadata = set_length(*the_metadata, size);
    asm volatile("mfence");

    AUDIT printk("%s: New metadata for block at offset %d (index %d) are %x", MOD_NAME, ret, get_index(ret), *the_metadata);

    memcpy(destination, the_metadata, MD_SIZE);
  
    printk("%s: Flushing changes into the device",MOD_NAME);

    bh = (struct buffer_head *)sb_bread(bdev_md.bdev->bd_super, ret);
    if (!bh)
    {
        AUDIT printk("%s: Error in retrieving the block at offset %d ", MOD_NAME, ret);
        ret = -EIO;
        goto exit;
    }
    the_dev_blk = (struct dev_blk *)bh->b_data;

    if (the_dev_blk != NULL)
    {
        memcpy(bh->b_data, destination, BLK_SIZE);
#ifndef SYNC_FLUSH
        mark_buffer_dirty(bh);
        the_block ->dirtiness = 0;
        asm volatile("mfence");
        AUDIT printk("%s: Page-cache write back-daemon will eventually flush changes into the device", MOD_NAME);
#else
        if (sync_dirty_buffer(bh) == 0)
        {
            AUDIT printk("%s: Synchronous flush succeded", MOD_NAME);
            the_block ->dirtiness = 0;
            asm volatile("mfence");
        }
        else
            printk("%s: Synchronous flush not succeded", MOD_NAME);
#endif
    }
    brelse(bh);  
exit:
    kfree(destination);
exit_2:
    spin_unlock(&(the_tree.write_lock));
    __sync_fetch_and_sub(&(bdev_md.open_count),1);
    return ret;
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
    unsigned long *epoch;
    unsigned long my_epoch;
    struct blk_element *the_block;
    char *file_path;
    int len = strlen(mount_md.mount_point);
    char *mount_point;
    struct file *filp;
    ssize_t msg_len = -1;
    int ret = 0;
    int index;

    printk("%s: SYS_GET_DATA \n", MOD_NAME);
    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        return -ENODEV;
    }

    if (size > SIZE)
        size = SIZE;
    if (size < 0 || offset > NBLOCKS - 1 || offset < 0)
        return -EINVAL;

    epoch = &the_tree.epoch;
    my_epoch = __sync_fetch_and_add(epoch, 1);

    the_block = lookup(the_tree.head, offset);

    if (the_block == NULL)
        return -EINVAL;
    msg_len = get_length(the_block->metadata);

    if (size > msg_len)
        size = msg_len;
    if (!get_validity(the_block->metadata))
        return 0;
    mount_point = (char *)kzalloc(sizeof(char) * len, GFP_KERNEL);
    strcpy(mount_point, mount_md.mount_point);
    mount_point[len - 1] = '\0';
    file_path = strcat(mount_point, "the-file");
    filp = filp_open(file_path, O_RDONLY, 0);
    if (IS_ERR(filp))
    {
        printk("%s: Failed to open file\n", MOD_NAME);
        return 0;
    }

    if (filp->f_op && filp->f_op->read)
    {
        filp->f_pos = BLK_SIZE * offset;

        ret = filp->f_op->read(filp, destination, size, &filp->f_pos);
        if (ret >= 0)
        {
            printk("%s: Read %d bytes from file\n", MOD_NAME, ret);
        }
        else
        {
            printk("%s: Failed to read from file\n", MOD_NAME);
        }
    }
    filp_close(filp, NULL);
    kfree(mount_point);
    index = (my_epoch & MASK) ? 1 : 0;
    __sync_fetch_and_add(&(the_tree.standing[index]), 1);
    return ret;
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

    struct buffer_head *bh;
    struct blk_element *the_block = NULL;
    struct dev_blk *blk = NULL;
    unsigned long last_epoch;
    unsigned long updated_epoch;
    unsigned long grace_period_threads;
    int index;
    // wait_queue_head_t invalidate_queue;
    DECLARE_WAIT_QUEUE_HEAD(invalidate_queue);
    printk("%s: SYS_INVALIDATE_DATA \n", MOD_NAME);

    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        return -ENODEV;
    }

    if (offset > NBLOCKS - 1 || offset < 0)
        return -EINVAL;

    __sync_fetch_and_add(&(bdev_md.open_count),1);
    spin_lock(&(the_tree.write_lock));

    AUDIT printk("%s: Traverse the tree to find block at offset %d", MOD_NAME, offset);
    
    the_block = lookup(the_tree.head, offset);
   //stampa_albero(the_tree.head);
    the_block->dirtiness = 1;
    asm volatile("mfence"); // make it visible to readers

    if (!get_validity(the_block->metadata))
    {
        spin_unlock(&(the_tree.write_lock));
        __sync_fetch_and_sub(&(bdev_md.open_count),1);
        return -ENODATA;
    }
    the_block->metadata = set_invalid(the_block->metadata);
    asm volatile("mfence"); // make it visible to readers

    // move to a new epoch - still under write lock
    updated_epoch = (the_tree.next_epoch_index) ? MASK : 0;

    the_tree.next_epoch_index += 1;
    the_tree.next_epoch_index %= 2;

    last_epoch = __atomic_exchange_n(&(the_tree.epoch), updated_epoch, __ATOMIC_SEQ_CST);
    index = (last_epoch & MASK) ? 1 : 0;
    grace_period_threads = last_epoch & (~MASK);

    AUDIT printk("%s: Deletion: waiting grace-full period (target value is %ld)\n", MOD_NAME, grace_period_threads);

    wait_event_interruptible(invalidate_queue, the_tree.standing[index] >= grace_period_threads);

    the_tree.standing[index] = 0;
    asm volatile("mfence"); // make it visible to readers
    //if (the_block) delete(the_tree.head, offset);
    
    // FLUSHING METADATA CHANGES INTO THE DEVICE
    printk("%s: Flushing metadata changes into the device",MOD_NAME);
    bh = (struct buffer_head *)sb_bread(bdev_md.bdev->bd_super, get_offset(offset));
    if (!bh)
    {
        AUDIT printk("%s: Error in retrieving the block at index %d", MOD_NAME, offset);
        __sync_fetch_and_sub(&(bdev_md.open_count),1);
        return -EIO;
    }
    blk = (struct dev_blk *)bh->b_data;

    if (blk != NULL)
    {
        memcpy(bh->b_data, &the_block->metadata, MD_SIZE);
#ifndef SYNC_FLUSH
        mark_buffer_dirty(bh);
        the_block ->dirtiness = 0;
        asm volatile("mfence");
        AUDIT printk("%s: Page-cache write back-daemon will eventually flush changes into the device", MOD_NAME);
#else
        if (sync_dirty_buffer(bh) == 0)
        {
            AUDIT printk("%s: Synchronous flush succeded", MOD_NAME);
            the_block ->dirtiness = 0;
            asm volatile("mfence");
        }
        else
            printk("%s: Synchronous flush not succeded", MOD_NAME);
#endif
    }
    brelse(bh);
    spin_unlock((&the_tree.write_lock));
    __sync_fetch_and_sub(&(bdev_md.open_count),1);
    return 1;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
long sys_invalidate_data = (unsigned long)__x64_sys_invalidate_data;
#else
#endif

static ssize_t dev_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    loff_t *my_off_ptr = get_off();
    int str_len = 0, index;
    struct blk_element *the_block;
    struct buffer_head *bh = NULL;
    struct dev_blk *blk = NULL;
    struct inode *the_inode = filp->f_inode;
    uint64_t file_size = the_inode->i_size;
    unsigned long my_epoch;
    // const char *dev_name = filp->f_path.dentry->d_iname;
    struct super_block *sb = filp->f_path.dentry->d_inode->i_sb;
    int ret = 0;
    loff_t offset;
    int block_to_read; // index of the block to be read from device

    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        return -ENODEV;
    }
    if (!bdev_md.open_count)
        return -EBADF; // The file should be open to invoke a read

    AUDIT printk("%s: Read operation called with len %ld - and offset %lld (the current file size is %lld)", MOD_NAME, len, *off, file_size);

    // check that *off is within boundaries
    *my_off_ptr = *off; // In this way each CPU has its own private copy and *off can't be changed concurrently
    if (*my_off_ptr >= file_size)
    {
        return 0;
    }

    // compute the actual index of the the block to be read from device
    block_to_read = *my_off_ptr / BLK_SIZE + 2; // the value 2 accounts for superblock and file-inode on device

    if (block_to_read > NBLOCKS + 2)
        return 0; // Consistency check

    my_epoch = __sync_fetch_and_add(&(the_tree.epoch), 1);
    AUDIT printk("%s: Read operation must access block %d of the device", MOD_NAME, block_to_read);

    the_block = lookup(the_tree.head, get_index(block_to_read));
 
    if (the_block == NULL || !get_validity(the_block->metadata) || get_free(the_block->metadata))
    {
        AUDIT printk("%s: The block at index %d is invalid or free", MOD_NAME, block_to_read);
        len = strlen("\n");
        ret = copy_to_user(buf, "\n", len);
        *my_off_ptr += BLK_SIZE;
        goto exit;
    }

    bh = (struct buffer_head *)sb_bread(sb, block_to_read);
    if (!bh)
    {
        AUDIT printk("%s: Error in retrieving the block %d", MOD_NAME, block_to_read);
        return -EIO;
    }

    blk = (struct dev_blk *)bh->b_data;

    if (blk != NULL)
    {
        str_len = get_length(blk->metadata);
        AUDIT printk("%s: Block at index %d has message with length %d", MOD_NAME, block_to_read, str_len);
        offset = *my_off_ptr % BLK_SIZE; // Residual

        AUDIT printk("%s: Reading the block at index %d with offset within the block %lld and residual bytes %lld", MOD_NAME, block_to_read, offset, str_len - offset);

        if (offset == 0)
            len = str_len;
        else if (offset < MD_SIZE + str_len)
            len = str_len - offset;
        else
            len = 0;

        ret = copy_to_user(buf, blk->data + offset, len);
        if (ret == 0)
            *my_off_ptr += BLK_SIZE - offset;
        else
            *my_off_ptr += len - ret;
    }
    else
        *my_off_ptr += BLK_SIZE;

    brelse(bh);
exit:
    *off = *my_off_ptr;
    index = (my_epoch & MASK) ? 1 : 0;
    __sync_fetch_and_add(&(the_tree.standing[index]), 1);
    return len - ret;
}

static int dev_release(struct inode *inode, struct file *filp)
{

    AUDIT printk("%s: Device release has been invoked: the thread %d trying to release the device file\n ", MOD_NAME, current->pid);

    if (!mount_md.mounted)
    {   
        __sync_fetch_and_sub(&(bdev_md.open_count), 1);
        printk("%s: The device is not mounted", MOD_NAME);
        return -ENODEV;
    }
    __sync_fetch_and_sub(&(bdev_md.open_count), 1);
    return 0;
}

static int dev_open(struct inode *inode, struct file *filp)
{

    AUDIT printk("%s: Device open has been invoked: the thread %d trying to open file at offset %lld\n ", MOD_NAME, current->pid, filp->f_pos);

    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        return -ENODEV;
    }

    if (filp->f_mode & FMODE_WRITE)
    {
        __sync_fetch_and_sub(&(bdev_md.open_count), 1);
        printk("%s: Write operation not allowed", MOD_NAME);
        return -EROFS;
    }
    // Avoiding that a concurrent thread may have killed the sb with unomunt operation
    __sync_fetch_and_add(&(bdev_md.open_count), 1);
    return 0;
}

// static loff_t dev_llseek(struct file *, loff_t off, int whence)
// {
//     AUDIT printk("%s: Device llseek has been invoked at offset %lld\n ", MOD_NAME, off);

//     return off;
// }

const struct file_operations dev_fops = {
    .owner = THIS_MODULE,
    .read = dev_read,
    .release = dev_release,
    .open = dev_open,
    //.llseek = dev_llseek
};
