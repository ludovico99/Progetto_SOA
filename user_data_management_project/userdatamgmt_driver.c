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

/*This function retrieves data from the device at a given offset and copies it to the user buffer.

Parameters:
int offset: An integer representing the index of the block from which data is to be retrieved.
char *destination: A pointer to the buffer where the retrieved data is to be stored.
ssize_t size: An integer value that specifies the maximum size of data to be retrieved.
Return Value:
Returns the number of bytes of data copied to the user buffer upon successful execution, otherwise returns an appropriate error code.*/
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

    // Atomically adds 1 to bdev_md.count variable and returns the new value.
    __sync_fetch_and_add(&(bdev_md.count), 1);
    printk("%s: SYS_PUT_DATA \n", MOD_NAME);

    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
         // Set the ret variable to -19 (error code for no such device).
        ret = -ENODEV;
        // Jumps to the exit label
        goto exit;
    }
     // If the size variable is greater than the SIZE constant, set its value to SIZE.
    if (size > SIZE)
        size = SIZE;
    if (size < 0){
        // Set the ret variable to -22 (error code for invalid argument).
        ret = -EINVAL;
        goto exit;
    }

    // Locally computing the newest metadata mask
    the_metadata = set_valid(the_metadata); //Sets the valid bit
    the_metadata = set_not_free(the_metadata); //clears the free bit
    the_metadata = set_length(the_metadata, size); //sets the length field of the_metadata variable equal to size

    destination = (char *)kzalloc(BLK_SIZE, GFP_KERNEL);

    if (!destination)
    {   
        printk("%s: Kzalloc has failed\n", MOD_NAME);
        // Releases the write lock of the sh_data structure.
        spin_unlock(&(sh_data.write_lock));
        // Set the ret variable to -12 (error code for out of memory).
        ret = -ENOMEM;
        goto exit;
    }
     // Copies data from user space to kernel space, starting at the address of the destination array plus the size of the metadata and with a maximum size equal to size. The number of bytes that could not be copied is stored in the residual_bytes variable.
    residual_bytes = copy_from_user(destination + MD_SIZE, source, size);
    AUDIT printk("%s: Copy_from_user residual bytes %ld", MOD_NAME, residual_bytes);
    // Copies the metadata field of the_block structure to the first MD_SIZE bytes of the destination array.
    memcpy(destination, &the_metadata, MD_SIZE);

    // Acquires the write lock of the sh_data structure.
    spin_lock(&(sh_data.write_lock));
     // Traverses the binary tree starting from sh_data.head and returns the first empty block encountered.
    the_block = inorderTraversal(sh_data.head);

    if (the_block == NULL)
    {   
        printk("%s: No blocks available in the device\n", MOD_NAME);
        // Releases the write lock of the sh_data structure.
        spin_unlock(&(sh_data.write_lock));
        // Set the ret variable to -12 (error code for out of memory).
        ret = -ENOMEM;
        goto exit;
    }

    // If the block already contains a message, assigns its reference to the_message variable. 
    // Otherwise, allocates memory for a new message with size equal to the sizeof(struct message) and assigns its reference to the_message variable.
    if (get_validity(the_block->metadata) && get_free(the_block->metadata))
        the_message = the_block->msg;
    else
        the_message = (struct message *)kzalloc(sizeof(struct message), GFP_KERNEL);

    the_message->prev = *the_tail;
    // Computes the offset value starting from the index value of the_block structure and assigns it to the ret variable.
    ret = get_offset(the_block->index);
    
    AUDIT printk("%s: Old metadata for block at offset %d (index %d) are %x", MOD_NAME, ret, get_index(ret), the_block->metadata);
    AUDIT printk("%s: New metadata for block at offset %d (index %d) are %x", MOD_NAME, ret, get_index(ret), the_metadata);
    
    //Updating the metadata mask of the block logically linked to the new valid message
    the_block->metadata = the_metadata;
    asm volatile("mfence");

    // Assigns the reference of the first message and the last message of the sh_data list to the_head and the_tail variables respectively.
    the_head = &sh_data.first;
    the_tail = &sh_data.last;

    if (*the_tail == NULL)
    {   
        // If sh_data list is empty, assigns the reference of the_message variable to both the_head and the_tail variables.

        *the_head = the_message;
        asm volatile("mfence");
        *the_tail = the_message;
        asm volatile("mfence");
        goto insert_completed;
    }
    // Otherwise, appends the_message variable at the end of the sh_data list.
    (*the_tail)->next = the_message;
    asm volatile("mfence");
    *the_tail = the_message;
    asm volatile("mfence");

insert_completed:
    // Assigns the reference to the_block structure to the elem field of the_message variable and the reference to the_message variable to the msg field of the_block structure.
    the_message->elem = the_block;
    asm volatile("mfence");
    the_block->msg = the_message;
    asm volatile("mfence");
    // Releases the write lock of the sh_data structure.
    spin_unlock(&(sh_data.write_lock));

    AUDIT printk("%s: Flushing changes into the device", MOD_NAME);
    // Reads a block from the device starting at the offset indicated by the ret variable.
    bh = (struct buffer_head *)sb_bread(bdev_md.bdev->bd_super, ret);
    if (!bh)
    {   
        printk("%s: Error in retrieving the block at offset %d ", MOD_NAME, ret);
        // Set the ret variable to -5 (error code for I/O error)
        ret = -EIO;
        // Jumps to the exit_2 label.
        goto exit_2;
    }
    the_dev_blk = (struct dev_blk *)bh->b_data;

    if (the_dev_blk != NULL)
    {   
        // Copies BLK_SIZE bytes from the destination array to the buffer data of the bh structure.
        memcpy(bh->b_data, destination, BLK_SIZE);
#ifndef SYNC_FLUSH
        // Sets the dirty bit of the bh structure and marks it as requiring writeback.  
        mark_buffer_dirty(bh);

        AUDIT printk("%s: Page-cache write back-daemon will eventually flush changes into the device", MOD_NAME);
#else
        if (sync_dirty_buffer(bh) == 0)
        {   
            AUDIT printk("%s: Synchronous flush succeded", MOD_NAME);
        }
        else
           printk("%s: Synchronous flush not succeded", MOD_NAME);
#endif
    }
    // Releases the buffer_head structure.
    brelse(bh);
exit_2:
    // Frees the memory allocated with kzalloc
    kfree(destination);
exit:
    // Atomically subtracts 1 from the bdev_md.count variable and returns the new value.
    __sync_fetch_and_sub(&(bdev_md.count), 1);
    // Returns the value of the ret variable.
    return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
long sys_put_data = (unsigned long)__x64_sys_put_data;
#else
#endif

/*This function retrieves data from the device at a given offset and copies it to the user buffer.
Parameters:
int offset: An integer representing the index of the block from which data is to be retrieved.
char *destination: A pointer to the buffer where the retrieved data is to be stored.
ssize_t size: An integer value that specifies the maximum size of data to be retrieved
Return Value:
Returns the number of bytes of data copied to the user buffer upon successful execution, otherwise returns an appropriate error code.*/
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

/*This function invalidates data at a given offset.

Parameters:
int offset: An integer representing the index of the block whose data is to be invalidated.
Return Value:
Returns 1 upon successful execution, otherwise returns an appropriate error code.*/
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

    the_block->metadata = set_invalid(the_block->metadata);
    asm volatile("mfence"); // make it visible to readers

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
        AUDIT printk("%s: Page-cache write back-daemon will eventually flush changes into the device", MOD_NAME);
#else
        if (sync_dirty_buffer(bh) == 0)
        {
            AUDIT printk("%s: Synchronous flush succeded", MOD_NAME);
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

/*This function reads data from the device.

Parameters:
struct file *filp: A pointer to a file object representing the file to be read.
char __user *buf: A buffer in user space where the read data is to be stored.
size_t len: An integer representing the number of bytes to be read.
loff_t *off: A pointer to the offset within the file from which the reading operation starts.
Return Value:
Returns the number of bytes read upon successful execution, otherwise returns an appropriate error code.*/
static ssize_t dev_read(struct file *filp, char __user *buf, size_t len, loff_t *off)
{
    int str_len = 0, index;
    struct blk_element *the_block;
    struct buffer_head *bh = NULL;
    struct dev_blk *blk = NULL;
    struct inode *the_inode = filp->f_inode;
    struct current_message *the_message = (struct current_message *)filp->private_data;
    loff_t my_off = the_message ->offset; // In this way each open has its own private copy and *off can't be changed concurrently
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
        printk("%s: Error in retrieving the block %d", MOD_NAME, block_to_read);
        index = (my_epoch & MASK) ? 1 : 0;
        __sync_fetch_and_add(&(sh_data.standing[index]), 1);
        return -EIO;
    }

    blk = (struct dev_blk *)bh->b_data;

    if (blk != NULL)
    {
        str_len = get_length(blk->metadata);
        AUDIT printk("%s: Block at index %d has message with length %d", MOD_NAME, get_index(block_to_read), str_len);
        offset = my_off % BLK_SIZE; // Residual

        AUDIT printk("%s: Reading the block at index %d with offset within the block %lld and residual bytes %lld", MOD_NAME, get_index(block_to_read), offset, str_len - offset);

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
/*This function is called when the device file is released by the user.

Parameters:
struct inode *inode: A pointer to the inode structure containing information about the device.
struct file *filp: A pointer to a file object representing the file being released.
Return Value:
Returns 0 upon successful execution, otherwise returns an appropriate error code.*/
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
/*This function is called when the device file is opened by a user.

Parameters:
struct inode *inode: A pointer to the inode structure containing information about the device.
struct file *filp: A pointer to a file object representing the file being opened.
Return Value:
Returns 0 upon successful execution, otherwise returns an appropriate error code.*/
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
