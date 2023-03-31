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
    struct blk_element *the_block = NULL;
    struct message **the_head = NULL;
    struct message **the_tail = NULL;
    struct message *the_message = NULL;
    struct dev_blk *the_dev_blk;
    struct buffer_head *bh;
    char *destination;
    uint16_t the_metadata = 0;
    unsigned long residual_bytes = -1;
    int ret = 0;

    __sync_fetch_and_add(&(bdev_md.count), 1);
    printk("%s: SYS_PUT_DATA \n", MOD_NAME);

    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        ret = -ENODEV;
        goto exit;
    }

    if (size > SIZE)
        size = SIZE;
    if (size < 0){
        ret = -EINVAL;
        goto exit;
    }


    spin_lock(&(sh_data.write_lock));

    the_block = inorderTraversal(sh_data.head);

    if (the_block == NULL)
    {
        printk("%s: No blocks available in the device\n", MOD_NAME);
        spin_unlock(&(sh_data.write_lock));
        ret = -ENOMEM;
        goto exit;
    }

    destination = (char *)kzalloc(BLK_SIZE, GFP_KERNEL);

    if (!destination)
    {
        printk("%s: Kzalloc has failed\n", MOD_NAME);
        spin_unlock(&(sh_data.write_lock));
        ret = -ENOMEM;
        goto exit;
    }

    residual_bytes = copy_from_user(destination + MD_SIZE, source, size);
    AUDIT printk("%s: Copy_from_user residual bytes %ld", MOD_NAME, residual_bytes);

    if (get_validity(the_block->metadata))
        the_message = the_block->msg;
    else
        the_message = (struct message *)kzalloc(sizeof(struct message), GFP_KERNEL);

    the_metadata = the_block->metadata;
    ret = get_offset(the_block->index);

    AUDIT printk("%s: Old metadata for block at offset %d (index %d) are %x", MOD_NAME, ret, get_index(ret), the_metadata);
    // Locally i compute the newest metadata mask
    the_metadata = set_valid(the_metadata);
    the_metadata = set_not_free(the_metadata);
    the_metadata = set_length(the_metadata, size);

    AUDIT printk("%s: New metadata for block at offset %d (index %d) are %x", MOD_NAME, ret, get_index(ret), the_metadata);
    // Update metadata mask in order to make it visible to all readers
    the_block->metadata = the_metadata;
    asm volatile("mfence");
    // the_block->dirtiness = 0;
    // asm volatile("mfence");

    // Insertion with cost O(1) in the messages list
    the_head = &sh_data.first;
    the_tail = &sh_data.last;
    the_message->prev = *the_tail;
    asm volatile("mfence");
    if (*the_tail == NULL)
    {
        *the_head = the_message;
        asm volatile("mfence");
        *the_tail = the_message;
        asm volatile("mfence");
        goto cont;
    }
    (*the_tail)->next = the_message;
    *the_tail = the_message;
    asm volatile("mfence");
    //stampa_lista(sh_data.first);
cont:
    the_message->elem = the_block;
    asm volatile("mfence");
    the_block->msg = the_message;
    asm volatile("mfence");

    spin_unlock(&(sh_data.write_lock));

    memcpy(destination, &(the_block->metadata), MD_SIZE);
  
    printk("%s: Flushing changes into the device", MOD_NAME);
    bh = (struct buffer_head *)sb_bread(bdev_md.bdev->bd_super, ret);
    if (!bh)
    {
        AUDIT printk("%s: Error in retrieving the block at offset %d ", MOD_NAME, ret);
        ret = -EIO;
        goto exit_2;
    }
    the_dev_blk = (struct dev_blk *)bh->b_data;

    if (the_dev_blk != NULL)
    {
        memcpy(bh->b_data, destination, BLK_SIZE);
#ifndef SYNC_FLUSH
        mark_buffer_dirty(bh);
        // the_block->dirtiness = 0;
        // asm volatile("mfence");
        AUDIT printk("%s: Page-cache write back-daemon will eventually flush changes into the device", MOD_NAME);
#else
        if (sync_dirty_buffer(bh) == 0)
        {
            AUDIT printk("%s: Synchronous flush succeded", MOD_NAME);
            // the_block->dirtiness = 0;
            // asm volatile("mfence");
        }
        else
            printk("%s: Synchronous flush not succeded", MOD_NAME);
#endif
    }
    brelse(bh);
exit_2:
    kfree(destination);
exit:
    __sync_fetch_and_sub(&(bdev_md.count), 1);
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
    struct buffer_head *bh;
    struct dev_blk *dev_blk;
    ssize_t msg_len = -1;
    int len = 0;
    int ret = -1;
    int index;
    loff_t off = 0;

    __sync_fetch_and_add(&(bdev_md.count), 1); // The unmount operation is not permitted
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


    epoch = &sh_data.epoch;
    my_epoch = __sync_fetch_and_add(epoch, 1);

    the_block = tree_lookup(sh_data.head, offset);

    if (the_block == NULL){
        ret = -EINVAL;
        goto exit;
    }
        

    msg_len = get_length(the_block->metadata);
    if (size > msg_len)
        size = msg_len;
    if (!get_validity(the_block->metadata)) {
        ret = 0;
        goto exit;
    }

    bh = (struct buffer_head *)sb_bread(bdev_md.bdev->bd_super, get_offset(offset));
    if (!bh)
    {
        AUDIT printk("%s: Error in retrieving the block at index %d", MOD_NAME, offset);
        ret = -EIO;
        goto exit;
    }
    dev_blk = (struct dev_blk *)bh->b_data;

    if (dev_blk != NULL)
    {
        while (ret != 0)
        {
            AUDIT printk("%s: Reading the block at index %d with offset within the block %lld and residual bytes %lld", MOD_NAME, get_offset(the_block->index), off, msg_len - off);
            if (off == 0)
                len = msg_len;
            else if (offset < MD_SIZE + msg_len)
                len = msg_len - off;
            else
                len = 0;

            ret = copy_to_user(destination, dev_blk->data + off, len); // Returns number of bytes that could not be copied
            off += len - ret;                                          // Residual
        }
        ret = len - ret;
    }
    brelse(bh);
exit:
    index = (my_epoch & MASK) ? 1 : 0;
    __sync_fetch_and_add(&(sh_data.standing[index]), 1);
    __sync_fetch_and_sub(&(bdev_md.count), 1);
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
    struct message *the_message;
    struct blk_element *the_block = NULL;
    struct dev_blk *blk = NULL;
    unsigned long last_epoch;
    unsigned long updated_epoch;
    unsigned long grace_period_threads;
    int index;
    int ret = 1;
    // wait_queue_head_t invalidate_queue;
    DECLARE_WAIT_QUEUE_HEAD(invalidate_queue);

     __sync_fetch_and_add(&(bdev_md.count), 1); // The unmount operation is not permitted
    printk("%s: SYS_INVALIDATE_DATA \n", MOD_NAME);

    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        ret = - ENODEV;
        goto exit;
    }

    if (offset > NBLOCKS - 1 || offset < 0){
        ret = - EINVAL;
        goto exit;
    }

    spin_lock(&(sh_data.write_lock));

    AUDIT printk("%s: Traverse the tree to find block at index %d", MOD_NAME, offset);

    the_block = tree_lookup(sh_data.head, offset);
    if (!get_validity(the_block->metadata))
    {
        spin_unlock(&(sh_data.write_lock));
        ret = -ENODATA;
        goto exit;
    
    }

    AUDIT printk("%s: Deleting the message from valid messages list...", MOD_NAME);

    the_message = the_block->msg;
    delete (&sh_data.first, &sh_data.last, the_message);

  

    // stampa_lista (sh_data.first);

    the_block->metadata = set_invalid(the_block->metadata);
    asm volatile("mfence"); // make it visible to readers
    /// the_block->dirtiness = 1;
    // asm volatile("mfence"); // make it visible to readers

    //  move to a new epoch - still under write lock
    updated_epoch = (sh_data.next_epoch_index) ? MASK : 0;

    sh_data.next_epoch_index += 1;
    sh_data.next_epoch_index %= 2;

    last_epoch = __atomic_exchange_n(&(sh_data.epoch), updated_epoch, __ATOMIC_SEQ_CST);
    index = (last_epoch & MASK) ? 1 : 0;
    grace_period_threads = last_epoch & (~MASK);

    AUDIT printk("%s: Invalidation: waiting grace-full period (target value is %ld)\n", MOD_NAME, grace_period_threads);

    wait_event_interruptible(invalidate_queue, sh_data.standing[index] >= grace_period_threads);
    sh_data.standing[index] = 0;

    spin_unlock((&sh_data.write_lock));

    AUDIT printk("%s: Removing invalidated message from valid messages list...\n", MOD_NAME);
    if (the_message)
        kfree(the_message);

    // FLUSHING METADATA CHANGES INTO THE DEVICE
    printk("%s: Flushing metadata changes into the device", MOD_NAME);
    bh = (struct buffer_head *)sb_bread(bdev_md.bdev->bd_super, get_offset(offset));
    if (!bh)
    {
        AUDIT printk("%s: Error in retrieving the block at index %d", MOD_NAME, offset);
        ret = -EIO;
        goto exit;
    }
    blk = (struct dev_blk *)bh->b_data;

    if (blk != NULL)
    {
        memcpy(bh->b_data, &the_block->metadata, MD_SIZE);
#ifndef SYNC_FLUSH
        mark_buffer_dirty(bh);
        // the_block->dirtiness = 0;
        // asm volatile("mfence");
        AUDIT printk("%s: Page-cache write back-daemon will eventually flush changes into the device", MOD_NAME);
#else
        if (sync_dirty_buffer(bh) == 0)
        {
            AUDIT printk("%s: Synchronous flush succeded", MOD_NAME);
            // the_block->dirtiness = 0;
            // asm volatile("mfence");
        }
        else
            printk("%s: Synchronous flush not succeded", MOD_NAME);
#endif
    }
    brelse(bh);
exit:
    __sync_fetch_and_sub(&(bdev_md.count), 1);
    return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
long sys_invalidate_data = (unsigned long)__x64_sys_invalidate_data;
#else
#endif

static ssize_t dev_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    int str_len = 0, index;
    struct blk_element *the_block;
    struct buffer_head *bh = NULL;
    struct dev_blk *blk = NULL;
    struct inode *the_inode = filp->f_inode;
    struct current_message *the_message = (struct current_message *)filp->private_data;
    loff_t my_off = the_message ->offset; // In this way each CPU has its own private copy and *off can't be changed concurrently
    uint64_t file_size = the_inode->i_size;
    unsigned long my_epoch;
    struct super_block *sb = filp->f_path.dentry->d_inode->i_sb;
    int ret = 0;
    loff_t offset;
    int block_to_read; // index of the block to be read from device

    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        return -ENODEV;
    }
    if (!bdev_md.count)
        return -EBADF; // The file should be open to invoke a read

    AUDIT printk("%s: Read operation called with len %ld - and offset %lld (the current file size is %lld)", MOD_NAME, len, *off, file_size);

    if (sh_data.first == NULL)
    { // No valid messages available
        return 0;
    }
    my_epoch = __sync_fetch_and_add(&(sh_data.epoch), 1);
    
    the_block = the_message->elem;
    // compute the actual index of the the block to be read from device
    block_to_read = my_off / BLK_SIZE + 2; // the value 2 accounts for superblock and file-inode on device

    // In the first read i want to read the head of the list
    if (the_message->index == -1)
    {   
        the_block = sh_data.first->elem;
        if (sh_data.first->elem != NULL) // Consistency check
            block_to_read = get_offset(sh_data.first->elem->index);
        else
        {
            index = (my_epoch & MASK) ? 1 : 0;
            __sync_fetch_and_add(&(sh_data.standing[index]), 1);
            return 0;
        }
    }
  
    // check that *off is within boundaries
    if (my_off >= file_size)
    {   
        index = (my_epoch & MASK) ? 1 : 0;
        __sync_fetch_and_add(&(sh_data.standing[index]), 1);
        return 0;
    }

    if (block_to_read > NBLOCKS + 2){// Consistency check
        index = (my_epoch & MASK) ? 1 : 0;
        __sync_fetch_and_add(&(sh_data.standing[index]), 1);
         return 0; 
    }
    
    AUDIT printk("%s: Read operation must access block %d of the device", MOD_NAME, block_to_read);

    bh = (struct buffer_head *)sb_bread(sb, block_to_read);
    if (!bh)
    {
        AUDIT printk("%s: Error in retrieving the block %d", MOD_NAME, block_to_read);
        index = (my_epoch & MASK) ? 1 : 0;
        __sync_fetch_and_add(&(sh_data.standing[index]), 1);
        return -EIO;
    }

    blk = (struct dev_blk *)bh->b_data;

    if (blk != NULL)
    {
        str_len = get_length(blk->metadata);
        AUDIT printk("%s: Block at index %d has message with length %d", MOD_NAME, block_to_read, str_len);
        offset = my_off % BLK_SIZE; // Residual

        AUDIT printk("%s: Reading the block at index %d with offset within the block %lld and residual bytes %lld", MOD_NAME, block_to_read, offset, str_len - offset);

        if (offset == 0)
            len = str_len;
        else if (offset < MD_SIZE + str_len)
            len = str_len - offset;
        else
            len = 0;

        ret = copy_to_user(buf, blk->data + offset, len);
        if (ret == 0 && the_block->msg->next == NULL)
        {
            my_off = file_size; // In this way the next read will end with code 0
            goto exit;
        }
        else if (ret == 0)
        {
            index = the_block->msg->next->elem->index;
            my_off = index * BLK_SIZE;
            the_message->elem = the_block->msg->next->elem;
            the_message->index = index;
        }
        else
            my_off += len - ret;
    }
    else
        my_off += BLK_SIZE;
exit:
    brelse(bh);
    *off = my_off;
    the_message->offset = my_off;
    index = (my_epoch & MASK) ? 1 : 0;
    __sync_fetch_and_add(&(sh_data.standing[index]), 1);
    return len - ret;
}

static int dev_release(struct inode *inode, struct file *filp)
{

    AUDIT printk("%s: Device release has been invoked: the thread %d trying to release the device file\n ", MOD_NAME, current->pid);

    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        return -ENODEV;
    }
    kfree(filp->private_data);
    __sync_fetch_and_sub(&(bdev_md.count), 1);
    return 0;
}

static int dev_open(struct inode *inode, struct file *filp)
{
    struct current_message *curr;
    AUDIT printk("%s: Device open has been invoked: the thread %d trying to open file at offset %lld\n ", MOD_NAME, current->pid, filp->f_pos);

    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        return -ENODEV;
    }
    if (filp->f_mode & FMODE_WRITE)
    {
        printk("%s: Write operation not allowed", MOD_NAME);
        return -EROFS;
    }
    curr = (struct current_message *)kzalloc(sizeof(struct current_message), GFP_KERNEL);
    if (!curr)
    {
        printk("%s: Error allocationg current_message struct\n", MOD_NAME);
        return -ENOMEM;
    }
    curr->index = -1;
    filp->private_data = curr;
    // Avoiding that a concurrent thread may have killed the sb with unomunt operation
    __sync_fetch_and_add(&(bdev_md.count), 1);
    return 0;
}

const struct file_operations dev_fops = {
    .owner = THIS_MODULE,
    .read = dev_read,
    .release = dev_release,
    .open = dev_open,
};
