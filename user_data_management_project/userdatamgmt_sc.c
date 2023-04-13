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
    if (size < 0)
    {
        // Set the ret variable to -22 (error code for invalid argument).
        ret = -EINVAL;
        goto exit;
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
        goto exit;
    }
    // Copies data from user space to kernel space, starting at the address of the destination array plus the size of the metadata and with a maximum size equal to size. The number of bytes that could not be copied is stored in the residual_bytes variable.
    residual_bytes = copy_from_user(destination + MD_SIZE, source, size);
    AUDIT printk("%s: Copy_from_user residual bytes %ld", MOD_NAME, residual_bytes);
    // Copies the metadata field of the_block structure to the first MD_SIZE bytes of the destination array.
    memcpy(destination, &the_metadata, MD_SIZE);

    // Allocates memory for a new message with size equal to the sizeof(struct message) and assigns its reference to the_message variable.
    the_message = (struct message *)kzalloc(sizeof(struct message), GFP_KERNEL);
    if (!the_message)
    {
        printk("%s: Kzalloc has failed\n", MOD_NAME);
        // Set the ret variable to -12 (error code for out of memory).
        ret = -ENOMEM;
        goto exit;
    }

    // Acquires the write lock of the sh_data structure.
    spin_lock(&(sh_data.write_lock));

    // Assigns the reference of the first message and the last message of the sh_data list to the_head and the_tail variables respectively.
    the_head = &sh_data.first;
    the_tail = &sh_data.last;

    // Traverses the binary tree starting from sh_data.head and returns the first empty block encountered.
    the_block = inorderTraversal(sh_data.head);

    if (the_block == NULL)
    {
        printk("%s: No blocks available in the device\n", MOD_NAME);
        // Releases the write lock of the sh_data structure.
        spin_unlock(&(sh_data.write_lock));
        // Set the ret variable to -12 (error code for out of memory).
        ret = -ENOMEM;
        goto exit_2;
    }

    // Computes the offset value starting from the index value of the_block structure and assigns it to the ret variable.
    ret = get_offset(the_block->index);

    AUDIT printk("%s: Old metadata for block at offset %d (index %d) are %x", MOD_NAME, ret, get_index(ret), the_block->metadata);
    AUDIT printk("%s: New metadata for block at offset %d (index %d) are %x", MOD_NAME, ret, get_index(ret), the_metadata);

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

        /*NOT EXPLOITED (get_free is always 0, false): If the block already contains a message, assigns its reference to the_message variable.
        if (get_validity(the_block->metadata) && get_free(the_block->metadata) the_message = the_block->msg;*/

        // Assigns the reference to the_block structure to the elem field of the_message variable and
        the_message->elem = the_block; // don't need locked operation since the new message isn't available to readers (if it's not valid and free)
        the_message->prev = *the_tail; // don't need locked operation since the new message isn't available to readers (if it's not valid and free)
        the_block->msg = the_message;  // Assigns the reference to the_message variable to the msg field of the_block structure.

        the_block->metadata = the_metadata;
        asm volatile("mfence");

        if (*the_tail == NULL)
        {
            // If sh_data list is empty, assigns the reference of the_message variable to both the_head and the_tail variables.
            *the_head = the_message;
            asm volatile("mfence");
            *the_tail = the_message;
            asm volatile("mfence");
            goto exit_2;
        }
        // Otherwise, appends the_message variable at the end of the sh_data list.
        (*the_tail)->next = the_message;
        asm volatile("mfence");
        *the_tail = the_message;
        asm volatile("mfence");
    }
    // Releases the buffer_head structure.
    brelse(bh);
exit_2:
    // Releases the write lock of the sh_data structure.
    spin_unlock(&(sh_data.write_lock));
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

    if (the_block == NULL)
    {
        ret = -EINVAL;
        goto exit;
    }

    msg_len = get_length(the_block->metadata);
    if (size > msg_len)
        size = msg_len;
    if (!get_validity(the_block->metadata))
    {
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
    uint16_t the_metadata;
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
        ret = -ENODEV;
        goto exit;
    }

    if (offset > NBLOCKS - 1 || offset < 0)
    {
        ret = -EINVAL;
        goto exit;
    }

    spin_lock(&(sh_data.write_lock));

    AUDIT printk("%s: Traverse the tree to find block at index %d", MOD_NAME, offset);

    the_block = tree_lookup(sh_data.head, offset);
    if (the_block == NULL)
    { // Consistency check: The shared structure should contains all block in the device
        spin_unlock(&(sh_data.write_lock));
        ret = -ENODATA;
        goto exit;
    }
    if (!get_validity(the_block->metadata))
    {
        spin_unlock(&(sh_data.write_lock));
        ret = -ENODATA;
        goto exit;
    }

    // FLUSHING METADATA CHANGES INTO THE DEVICE
    printk("%s: Flushing metadata changes into the device", MOD_NAME);
    bh = (struct buffer_head *)sb_bread(bdev_md.bdev->bd_super, get_offset(offset));
    if (!bh)
    {
        AUDIT printk("%s: Error in retrieving the block at index %d", MOD_NAME, offset);
        spin_unlock((&sh_data.write_lock));
        ret = -EIO;
        goto exit;
    }
    blk = (struct dev_blk *)bh->b_data;

    if (blk != NULL)
    {   
        the_metadata = set_invalid(the_block->metadata);
        memcpy(bh->b_data, &the_metadata, MD_SIZE);
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
        brelse(bh);

        AUDIT printk("%s: Deleting the message from valid messages list...", MOD_NAME);

        the_message = the_block->msg;
        delete (&sh_data.first, &sh_data.last, the_message);

        the_block->metadata = the_metadata;
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

        // The block has no corrispondence in valid message list
        the_block->msg = NULL;
        asm volatile("mfence"); // make it visible to readers

        spin_unlock((&sh_data.write_lock));
        AUDIT printk("%s: Removing invalidated message from valid messages list...\n", MOD_NAME);
        if (the_message) kfree(the_message);

    }
exit:
    __sync_fetch_and_sub(&(bdev_md.count), 1);
    return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
long sys_invalidate_data = (unsigned long)__x64_sys_invalidate_data;
#else
#endif