#define EXPORT_SYMTAB

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
    int ret = 0, i;

    // Atomically adds 1 to bdev_md.count variable and returns the new value.
    __sync_fetch_and_add(&(bdev_md.count), 1);
    AUDIT printk("%s: SYS_PUT_DATA \n", MOD_NAME);

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
    AUDIT printk("%s: Copy_from_user residual bytes %ld", MOD_NAME, residual_bytes);
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
    printk("%s: Flushing changes into the device", MOD_NAME);
    bh = (struct buffer_head *)sb_bread(bdev_md.bdev->bd_super, ret);
    if (!bh)
    {
        printk("%s: Error in retrieving the block at offset %d ", MOD_NAME, ret);
        // Set the ret variable to -5 (error code for I/O error)
        ret = -EIO;
        // Jumps to the exit label.
        goto all;
    }
    the_dev_blk = (struct dev_blk *)bh->b_data;

    if (the_dev_blk != NULL)
    {
        // Copies BLK_SIZE bytes from the destination array to the buffer data of the bh structure.
        memcpy(bh->b_data, destination, BLK_SIZE);

#ifndef SYNC_FLUSH
        // Sets the dirty bit of the bh structure and marks it as requiring writeback.
        mark_buffer_dirty(bh);

        AUDIT printk("%s: Page-cache write-back daemon will eventually flush changes into the device", MOD_NAME);
#else
        if (sync_dirty_buffer(bh) == 0)
        {
            AUDIT printk("%s: Synchronous flush succeded", MOD_NAME);
        }
        else
        {
            printk("%s: Synchronous flush not succeded", MOD_NAME);
            brelse(bh);
            ret = -EIO;
            goto all;
        }
#endif
        // Releases the buffer_head structure.
        brelse(bh);
        // If the device driver update is properly set, the RCU structure update can start
        AUDIT printk("%s: Updating the kernel structures ...", MOD_NAME);
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
        AUDIT printk("%s: Appending the newest message in the double linked list...", MOD_NAME);
        the_message->position = (*the_tail)->position + 1;

        *the_tail = the_message;

        the_message->prev->next = the_message; // Linearization point for the readers
        asm volatile("mfence");
    }
all:
    // Releases the write lock of the sh_data structure.
    spin_unlock(&(sh_data.write_lock));
    // Frees the memory allocated with kzalloc
free:
    kfree(destination);

fetch_and_sub:
    // Atomically subtracts 1 from the bdev_md.count variable and returns the new value.
    __sync_fetch_and_sub(&(bdev_md.count), 1);
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
    AUDIT printk("%s: SYS_GET_DATA \n", MOD_NAME);
    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        return -ENODEV;
    }

    if (size > SIZE)
        size = SIZE;
    if (size < 0 || offset > nblocks - 1 || offset < 0)
    {
        printk("%s: The offset is not valid", MOD_NAME);
        return -EINVAL;
    }

    // Adding 1 to the current epoch
    my_epoch = __sync_fetch_and_add(&sh_data.epoch, 1);

    // Searching the message with the specified offset in the valid messages list
    the_message = lookup(sh_data.first, offset);
    if (the_message == NULL)
    {
        printk("%s: The message is invalid", MOD_NAME);
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
            AUDIT printk("%s: Reading the block at index %d with offset within the block %lld and residual bytes %lld", MOD_NAME, get_offset(offset), off, msg_len - off);
            if (off == 0)
                len = size;
            else if (off < size)
                len = size - off;
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
    __sync_fetch_and_sub(&(bdev_md.count), 1); // The unmount operation is permitted
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

    struct buffer_head *bh = NULL;
    struct dev_blk *blk = NULL;
    struct message *the_message = NULL;
    struct blk_element *the_block = NULL;
    uint16_t the_metadata;
    unsigned long last_epoch;
    unsigned long updated_epoch;
    unsigned long grace_period_threads;
    int index;
    int ret = 0;

    DECLARE_WAIT_QUEUE_HEAD(invalidate_queue);

    __sync_fetch_and_add(&(bdev_md.count), 1); // The unmount operation is not permitted
    AUDIT printk("%s: SYS_INVALIDATE_DATA \n", MOD_NAME);

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

    AUDIT printk("%s: Accessing metadata for the block at index %d", MOD_NAME, offset);
    // Accessing to the block's metadata with offset as index
    the_block = head[offset];
    if (the_block == NULL) // Consistency check: The array should contains all block in the device
    {
        printk("%s: The block with the specified offset has no device matches", MOD_NAME);
        ret = -ENODATA;
        // Releasing the write lock ...
        spin_unlock((&sh_data.write_lock));
        goto exit;
    }
    if (!get_validity(the_block->metadata)) // The block with the specified offset has been already invalidated
    {
        printk("%s: The block with the specified offset has been already invalidated", MOD_NAME);
        ret = -ENODATA;
        // Releasing the write lock ...
        spin_unlock((&sh_data.write_lock));
        goto exit;
    }
    // FLUSHING CHANGES INTO THE DEVICE
    AUDIT printk("%s: Flushing changes into the device", MOD_NAME);
    bh = (struct buffer_head *)sb_bread(bdev_md.bdev->bd_super, get_offset(offset));
    if (!bh)
    {
        printk("%s: Error in retrieving the block at index %d", MOD_NAME, offset);
        ret = -EIO;
        // Releasing the write lock ...
        spin_unlock((&sh_data.write_lock));
        goto exit;
    }
    blk = (struct dev_blk *)bh->b_data;
    if (blk != NULL)
    {
        the_metadata = set_invalid(the_block->metadata);
        memcpy(&(blk->metadata), &the_metadata, MD_SIZE - POS_SIZE);

        blk->pos = INVALID_POSITION;

#ifndef SYNC_FLUSH
        mark_buffer_dirty(bh);

        AUDIT printk("%s: Page-cache write back-daemon will eventually flush changes into the device", MOD_NAME);
#else
        if (sync_dirty_buffer(bh) == 0)
        {
            AUDIT printk("%s: Synchronous flush succeded", MOD_NAME);
        }
        else
        {
            printk("%s: Synchronous flush not succeded", MOD_NAME);
            ret = -EIO;
            spin_unlock((&sh_data.write_lock));
            goto exit;
        }
#endif
        brelse(bh);

        // If the device driver update is properly set, the RCU structure update can start
        AUDIT printk("%s: Deleting the message from valid messages list...", MOD_NAME);

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

        AUDIT printk("%s: Removing invalidated message from valid messages list...\n", MOD_NAME);
        // Finally the grace period endeded and the message can be released ...
        if (the_message)
            kfree(the_message);
    }
exit:
    __sync_fetch_and_sub(&(bdev_md.count), 1); // The unmount operation is permitted
    return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
long sys_invalidate_data = (unsigned long)__x64_sys_invalidate_data;
#else
#endif