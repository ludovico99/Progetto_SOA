#define EXPORT_SYMTAB

DECLARE_WAIT_QUEUE_HEAD(invalidate_queue);
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
    struct buffer_head *bh = NULL;
    char *destination;
    uint16_t the_metadata = 0;
    unsigned long residual_bytes = -1;
    int ret = 0, i;

    // Atomically adds 1 to bdev_md.count variable and returns the new value.
    __sync_fetch_and_add(&(bdev_md.count), 1);
    AUDIT printk("%s: Thread with PID %d is executing SYS_PUT_DATA \n", MOD_NAME, current->pid);

    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        // Set the ret variable to -19 (error code for no such device).
        ret = -ENODEV;
        // Jumps to the exit label
        goto fetch_and_sub;
    }
    // If the size variable is greater than the SIZE constant, set its value to SIZE.
    if (size > SIZE)
        size = SIZE;
    if (size < 0)
    {
        // Set the ret variable to -22 (error code for invalid argument).
        ret = -EINVAL;
        goto fetch_and_sub;
    }

    // Locally computing the newest metadata mask
    the_metadata = set_valid(the_metadata);        // Sets the valid bit
    the_metadata = set_not_free(the_metadata);     // clears the free bit
    the_metadata = set_length(the_metadata, size); // sets the length field of the_metadata variable equal to size

    destination = (char *)kzalloc(BLK_SIZE, GFP_KERNEL);

    if (!destination)
    {
        printk("%s: Kzalloc has failed\n", MOD_NAME);
        // Set the ret variable to -12 (error code for out of memory).
        ret = -ENOMEM;
        goto fetch_and_sub;
    }
    // Copies data from user space to kernel space, starting at the address of the destination array plus the size of the metadata and with a maximum size equal to size. The number of bytes that could not be copied is stored in the residual_bytes variable.
    residual_bytes = copy_from_user(destination, source, size);

    // Copies the metadata field of the_block structure to the first MD_SIZE bytes of the destination array.
    memcpy(destination + SIZE, &the_metadata, MD_SIZE - POS_SIZE);

    // Allocates memory for a new message with size equal to the sizeof(struct message) and assigns its reference to the_message variable.
    the_message = (struct message *)kzalloc(sizeof(struct message), GFP_KERNEL);
    if (!the_message)
    {
        printk("%s: Kzalloc has failed\n", MOD_NAME);
        // Set the ret variable to -12 (error code for out of memory).
        ret = -ENOMEM;
        goto free;
    }

    // Acquires the write lock of the sh_data structure.
    spin_lock(&(sh_data.write_lock));

    // Assigns the reference of the first message and the last message of the sh_data list to the_head and the_tail variables respectively.
    the_head = &sh_data.first;
    the_tail = &sh_data.last;

    // Traverses the array starting from the first element and returns the first empty block encountered.
    for (i = 0; i < nblocks; i++)
    {
        if (head[i] == NULL)
            continue; // Consistency check

        if (!get_validity(head[i]->metadata))
        { // Finding the first invalid block into the array of metadata
            the_block = head[i];
            // Computes the offset value starting from the index value of the_block structure and assigns it to the ret variable.
            ret = get_offset(i);
            break;
        }
    }

    if (the_block == NULL)
    {
        printk("%s: No blocks available in the device\n", MOD_NAME);
        kfree(the_message);
        // Set the ret variable to -12 (error code for out of memory).
        ret = -ENOMEM;
        goto all;
    }

    AUDIT
    {
        printk("%s: Old metadata for block at offset %d (index %d) are %x", MOD_NAME, ret, get_index(ret), the_block->metadata);
        printk("%s: New metadata for block at offset %d (index %d) are %x", MOD_NAME, ret, get_index(ret), the_metadata);
    }
    // Reads a block from the device starting at the offset indicated by the ret variable.
    printk("%s: Thread with PID %d is flushing changes into the device", MOD_NAME, current->pid);
    bh = (struct buffer_head *)sb_bread(bdev_md.bdev->bd_super, ret);
    if (!bh)
    {
        printk("%s: Error in retrieving the block at offset %d ", MOD_NAME, ret);
        kfree(the_message);
        // Set the ret variable to -5 (error code for I/O error)
        ret = -EIO;
        // Jumps to the exit label.
        goto all;
    }
    the_dev_blk = (struct dev_blk *)bh->b_data;
    if (the_dev_blk == NULL)
    {
        printk("%s: Blk content is NULL", MOD_NAME);
        kfree(the_message);
        ret = -EINVAL;
        goto all;
    }
    else
    {
        // Copies BLK_SIZE bytes from the destination array to the buffer data of the bh structure.
        memcpy(bh->b_data, destination, BLK_SIZE);
        // Sets the dirty bit of the bh structure and marks it as requiring writeback.
        mark_buffer_dirty(bh);

#ifndef SYNC_FLUSH

        AUDIT printk("%s: Page-cache write-back daemon will flush changes into the device", MOD_NAME);
#else
#endif
// Releases the buffer_head structure.
#ifndef SYNC_FLUSH
        brelse(bh);
#endif
        // If the device driver update is properly set, the RCU structure update can start
        AUDIT printk("%s: Thread with PID %d is updating the kernel structures ...", MOD_NAME, current->pid);
        // Don't need locked / synchronizing operation since the new message isn't available to readers
        the_message->index = get_index(ret);
        the_message->elem = the_block;
        the_message->prev = *the_tail;

        // Don't need locked / synchronizing operation since no readers have to traverse the array of metadata
        the_block->msg = the_message; // Assigns the reference to the_message variable to the msg field of the_block structure.
        the_block->metadata = the_metadata;

        if (*the_tail == NULL)
        {
            // If sh_data list is empty, assigns the reference of the_message variable to both the_head and the_tail variables.
            AUDIT printk("%s: List is empty", MOD_NAME);

            the_message->position = 0;

            *the_tail = the_message;

            *the_head = the_message; // Linearization point for the readers
            asm volatile("mfence");

            goto all;
        }

        // Otherwise, appends the_message variable at the end of the sh_data list.
        AUDIT printk("%s: Thread with PID %d is appending the newest message in the double linked list...", MOD_NAME, current->pid);
        the_message->position = (*the_tail)->position + 1;

        *the_tail = the_message;

        the_message->prev->next = the_message; // Linearization point for the readers
        asm volatile("mfence");

        AUDIT printk("%s: Thread with PID %d has correctly completed the put operation", MOD_NAME, current->pid);
    }

all:
    // Releases the write lock of the sh_data structure.
    spin_unlock(&(sh_data.write_lock));

#ifdef SYNC_FLUSH
    if (ret >= 0)
    {
        if (sync_dirty_buffer(bh) == 0)
        {
            AUDIT printk("%s: Synchronous flush succeded", MOD_NAME);
        }
        else
        {
            printk("%s: Synchronous flush not succeded", MOD_NAME);
        }
    } brelse(bh);
#endif

free:
    // Frees the memory allocated with kzalloc
    kfree(destination);

fetch_and_sub:
    // Atomically subtracts 1 from the bdev_md.count variable and returns the new value.
    __sync_fetch_and_sub(&(bdev_md.count), 1);
    wake_up_interruptible(&unmount_queue);
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
    unsigned long my_epoch;
    struct message *the_message = NULL;
    struct buffer_head *bh = NULL;
    struct dev_blk *dev_blk = NULL;
    ssize_t msg_len = -1;
    int len = 0;
    int ret = -1;
    int index;
    loff_t off = 0;

    __sync_fetch_and_add(&(bdev_md.count), 1); // The unmount operation is not permitted
    AUDIT printk("%s: Thread with PID %d is executing SYS_GET_DATA \n", MOD_NAME, current->pid);
    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        __sync_fetch_and_sub(&(bdev_md.count), 1); // The unmount operation is permitted
        wake_up_interruptible(&unmount_queue);
        return -ENODEV;
    }

    if (size > SIZE)
        size = SIZE;
    if (size < 0 || offset > nblocks - 1 || offset < 0)
    {
        printk("%s: The offset is not valid", MOD_NAME);
        __sync_fetch_and_sub(&(bdev_md.count), 1); // The unmount operation is permitted
        wake_up_interruptible(&unmount_queue);
        return -EINVAL;
    }

    // Adding 1 to the current epoch
    my_epoch = __sync_fetch_and_add(&(sh_data.epoch), 1);

    // Searching the message with the specified offset in the valid messages list
    the_message = lookup_by_index(sh_data.first, offset);
    if (the_message == NULL)
    {
        printk("%s: The message at index %d is invalid", MOD_NAME, offset);
        ret = -ENODATA;
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

        msg_len = get_length(dev_blk->metadata);
        if (size > msg_len)
            size = msg_len;

        while (ret != 0)
        {
            AUDIT printk("%s: Thread with PID %d is reading the block at index %d with offset within the block %lld and residual bytes %lld", MOD_NAME, current->pid, get_offset(offset), off, msg_len - off);
            if (off == 0)
                len = size;
            else if (off < size)
                len = size - off;
            else
                len = 0;

            ret = copy_to_user(destination, dev_blk->data + off, len); // Returns number of bytes that could not be copied
            off += len - ret;                                          // Residual bytes
        }
        AUDIT printk("%s: Thread with PID %d has completed the read operation ...", MOD_NAME, current->pid);
        ret = len - ret;
    }
    brelse(bh);
exit:

    index = (my_epoch & MASK) ? 1 : 0;
    __sync_fetch_and_add(&(sh_data.standing[index]), 1); // Releasing a token in the correct epoch counter
    wake_up_interruptible(&invalidate_queue);

    __sync_fetch_and_sub(&(bdev_md.count), 1); // The unmount operation is permitted
    wake_up_interruptible(&unmount_queue);     // tell poll that data is ready
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

    struct message *the_message = NULL;
    struct blk_element *the_block = NULL;
    uint16_t the_metadata;
    unsigned long last_epoch;
    unsigned long updated_epoch;
    unsigned long grace_period_threads;
    int index;
    int ret = 0;

    __sync_fetch_and_add(&(bdev_md.count), 1); // The unmount operation is not permitted
    AUDIT printk("%s: The thread %d is executing SYS_INVALIDATE_DATA\n", MOD_NAME, current->pid);

    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        ret = -ENODEV;
        goto exit;
    }

    if (offset > nblocks - 1 || offset < 0)
    {
        printk("%s: Offset is invalid", MOD_NAME);
        ret = -EINVAL;
        goto exit;
    }

    // Acquiring the lock to avoid writers concurrency ...
    spin_lock(&(sh_data.write_lock));

    AUDIT printk("%s: Thread with PID %d is accessing metadata for the block at index %d", MOD_NAME, current->pid, offset);
    // Accessing to the block's metadata with offset as index
    the_block = head[offset];
    if (the_block == NULL) // Consistency check: The array should contains all block in the device
    {
        // Releasing the write lock ...
        spin_unlock((&sh_data.write_lock));
        printk("%s: The block with the specified offset has no device matches", MOD_NAME);
        ret = -ENODATA;
        goto exit;
    }
    if (!get_validity(the_block->metadata)) // The block with the specified offset has been already invalidated
    {
        // Releasing the write lock ...
        spin_unlock((&sh_data.write_lock));
        printk("%s: The block with the specified offset has been already invalidated", MOD_NAME);
        ret = -ENODATA;
        goto exit;
    }
    // The RCU structure update can start...
    AUDIT printk("%s: Thread with PID %d is deleting the message from valid messages list...", MOD_NAME, current->pid);

    // Get the pointer to the message linked to the block that has to be invalidated ...
    the_message = the_block->msg;
    the_block->msg = NULL;

    // Deleting the message from the double linked list ...
    delete (&sh_data.first, &sh_data.last, the_message);

    the_block->metadata = the_metadata;

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

    // Releasing the write lock ...
    spin_unlock((&sh_data.write_lock));

    AUDIT printk("%s: Thread with PID %d is releasing the buffer of the invalidate message ...\n", MOD_NAME, current->pid);
    // Finally the grace period endeded and the message can be released ...
    if (the_message)
        kfree(the_message);
exit:
    __sync_fetch_and_sub(&(bdev_md.count), 1); // The unmount operation is permitted
    wake_up_interruptible(&unmount_queue);
    return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
long sys_invalidate_data = (unsigned long)__x64_sys_invalidate_data;
#else
#endif