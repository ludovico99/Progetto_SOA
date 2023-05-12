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
    int str_len = 0, index, size = 0;
    unsigned long my_epoch;
    struct buffer_head *bh = NULL;
    struct dev_blk *blk = NULL;
    struct current_message *the_message = (struct current_message *)filp->private_data;
    struct message *curr_msg;            // To interate throught the double linked list
    loff_t my_off = the_message->offset; // In this way each open has its own private copy and *off can't be changed concurrently
    struct super_block *sb = filp->f_path.dentry->d_inode->i_sb;
    int ret = 0;
    bool read_residual_flag = false;
    loff_t bytes_copied = 0; // Copied bytes from the device
    loff_t offset = 0;       // within the block
    int block_to_read;       // index of the block to be read from device

    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        return -ENODEV;
    }
    if (!bdev_md.count)
    {
        printk("%s:  The file should be open to invoke a read", MOD_NAME);
        return -EBADF;
    }

    printk("%s: Read operation called with len %ld - and offset %lld (the current file size is %lld)", MOD_NAME, len, *off, file_size);

    if (rcu.first == NULL)
    { // No valid messages available
        return 0;
    }

    // check that *off is within boundaries
    if (my_off >= file_size)
    {
        return 0;
    }

    my_epoch = __sync_fetch_and_add(&(rcu.epoch), 1);

    if (the_message->insert_index == INVALID)
    // It means that it's the first read of the I/O session and want to read from offset 0. I assume that he wants to read all the data
    {
        AUDIT printk("%s: Reading the first block...", MOD_NAME);
        the_message->curr = rcu.first;
    }

    if (my_off % SIZE != 0) // It means that he wants to read residual bytes of the same block
    {
        AUDIT printk("%s: Reading the block partially read in the previous dev_read ...", MOD_NAME);
        read_residual_flag = true;
    }

    /*WARNING:
           The block concurrently may have been invalidated:
           - the->message->curr == NULL is a consistency check
           - the_message->curr->prev == NULL means that the writer released the buffer
           - the_message->curr->prev->next != the_message->curr means that the writer has linearized the invalidation
       */
    if (the_message->curr != NULL && the_message->curr == rcu.first)
        goto read;
    else if (the_message->curr == NULL || the_message->curr->prev == NULL || the_message->curr->prev->next != the_message->curr)
    {
        // The current block has been invalidated. Let's read the following one. Don't need to read residual bytes of the following message
        read_residual_flag = false;

        the_message->insert_index++;

        the_message->curr = lookup_by_insert_index(rcu.first, the_message->insert_index);
        if (the_message->curr == NULL) // There are no other valid messages
        {
            my_off = file_size;
            the_message->insert_index = INVALID;
            goto release_token;
        }
    }

read:

    curr_msg = the_message->curr;
    AUDIT printk("%s: The index of the first block to be read is %d", MOD_NAME, curr_msg->index);
    while (curr_msg != NULL)
    {
        // Updating the I/O session fields ...
        the_message->insert_index = curr_msg->ordering.insert_index;
        the_message->curr = curr_msg;

        if (!read_residual_flag)               // Don't want to read residual bytes of the same block
            my_off = (curr_msg->index) * SIZE; // Computing the new offset within the device file
        else
            read_residual_flag = false;

        block_to_read = my_off / SIZE + 2; // the value 2 accounts for superblock and file-inode on device

        // Consistency check: block_to_read must be less or equal than nblocks + 2
        if (block_to_read > nblocks + 2)
        {
            printk("%s: block_to_read is greater than nblocks + 2", MOD_NAME);
            ret = -ENODATA;
            goto release_token;
        }

        AUDIT printk("%s: Read operation must access block %d of the device", MOD_NAME, block_to_read);

        // Reading the block at offset block_to_read in the device
        bh = (struct buffer_head *)sb_bread(sb, block_to_read);
        if (!bh)
        {
            printk("%s: Error in retrieving the block %d", MOD_NAME, block_to_read);
            ret = -EIO;
            goto release_token;
        }

        blk = (struct dev_blk *)bh->b_data;

        if (blk != NULL)
        {
            str_len = get_length(blk->metadata); // str_len is initialized to the message length
            offset = my_off % SIZE;              // Residual bytes
            AUDIT
            {
                printk("%s: Block at index %d has message with length %d", MOD_NAME, get_index(block_to_read), str_len);
                printk("%s: Reading the block at index %d with offset within the block %lld and residual bytes %lld", MOD_NAME, get_index(block_to_read), offset, str_len - offset);
            }

            if (offset == 0)
                size = str_len;
            else if (offset < str_len)
                size = str_len - offset;
            else // if offset != 0 and >= str_len means that the message has been already read
                size = 0;

            if (len < size)
                size = len; // If the bytes to read is less than size then copy_to_user will read len bytes

            // Copy size bytes of message into the user buffer
            ret = copy_to_user(buf + bytes_copied, blk->data + offset, size);
            bytes_copied += size - ret; // Updating the counter of the number of bytes copied into the user buffer
            len -= size - ret;          // The user buffer size is reduced by size - ret (bytes read in the last copy_to_user)

            if (len == 0 || ret != 0)
            {
                AUDIT
                {
                    printk("%s: The buffer is full: Unable to copy all data to the user ...", MOD_NAME);
                    printk("%s: The index of the block to be read in the following dev_read is %d", MOD_NAME, get_index(block_to_read));
                }

                my_off += size - ret;
                brelse(bh);
                goto exit;
            }
        }

        // Reading the following message, if it's valid
        curr_msg = curr_msg->next;

        brelse(bh);
    }

exit:
    ret = bytes_copied;

    if (curr_msg == NULL)
    {
        printk("%s: Device read completed ...", MOD_NAME);

        my_off = file_size;
        the_message->insert_index = INVALID;
    }

release_token:
    index = (my_epoch & MASK) ? 1 : 0;
    __sync_fetch_and_add(&(rcu.standing[index]), 1);

    wake_up_interruptible(&wait_queue);

    *off = my_off;
    the_message->offset = my_off;
    return ret;
}

/*This function is called when the device file is released by the user.

Parameters:
struct inode *inode: A pointer to the inode structure containing information about the device.
struct file *filp: A pointer to a file object representing the file being released.
Return Value:
Returns 0 upon successful execution, otherwise returns an appropriate error code.*/
static int dev_release(struct inode *inode, struct file *filp)
{
    AUDIT printk("%s: Device release has been invoked by the thread %d\n ", MOD_NAME, current->pid);

    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        return -ENODEV;
    }

    if (filp->private_data != NULL)
        kfree(filp->private_data);
    __sync_fetch_and_sub(&(bdev_md.count), 1);
    wake_up_interruptible(&unmount_queue);
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
    AUDIT printk("%s: Device open has been invoked by the thread %d", MOD_NAME, current->pid);

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
    // Avoiding that a concurrent thread may have killed the sb with unomunt operation
    __sync_fetch_and_add(&(bdev_md.count), 1);
    curr = (struct current_message *)kzalloc(sizeof(struct current_message), GFP_KERNEL);
    if (!curr)
    {
        printk("%s: Error allocationg current_message struct", MOD_NAME);
        return -ENOMEM;
    }

    curr->insert_index = INVALID;
    filp->private_data = curr;

    return 0;
}

const struct file_operations dev_fops = {
    .owner = THIS_MODULE,
    .read = dev_read,
    .release = dev_release,
    .open = dev_open,
};
