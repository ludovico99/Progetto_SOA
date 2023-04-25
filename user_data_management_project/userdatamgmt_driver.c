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
    struct blk_element *the_block;
    struct buffer_head *bh = NULL;
    struct dev_blk *blk = NULL;
    struct inode *the_inode = filp->f_inode;
    struct current_message *the_message = (struct current_message *)filp->private_data;
    loff_t my_off = the_message->offset; // In this way each open has its own private copy and *off can't be changed concurrently
    uint64_t file_size = the_inode->i_size;
    struct super_block *sb = filp->f_path.dentry->d_inode->i_sb;
    int ret = 0;
    loff_t offset;     // within the block
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

    // check that *off is within boundaries
    if (my_off >= file_size)
    {
        return 0;
    }

    my_epoch = __sync_fetch_and_add(&(sh_data.epoch), 1);

    if (my_off % BLK_SIZE != 0)
    { // It means that he wants to read residual bytes of the same block

        /* The block concurrently has been invalidated: 
            - the->message->curr == NULL is a consistency check
            - the_message->curr->prev == NULL means that the writer released the buffer
            - the_message->curr->prev->next != the_message->curr means that the writer has linearized the invalidation
        */
        if (the_message-> curr != NULL && the_message->curr == sh_data.first) goto read_same_block;
        else if (the_message->curr == NULL || the_message->curr->prev == NULL || the_message->curr->prev->next != the_message->curr) goto release_token;
        else goto read_same_block;
    }

    if (the_message->position == INVALID_POSITION)
    // It means that it's the first read of the I/O session and want to read from offset 0. I assume that he wants to read all the data
    {
        AUDIT printk("%s: Reading the first block...", MOD_NAME);
        the_message->curr = sh_data.first;
    }
    // Reading the following block in the valid message list
    else
    {
        AUDIT printk("%s: Computing the following block to read...", MOD_NAME);
        the_message->curr = lookup_by_pos(sh_data.first, the_message->position);
        if (the_message->curr == NULL)
        { // There are no other valid messages
            my_off = file_size;
            the_message->position = INVALID_POSITION;
            goto release_token;
        }
    }

    the_message->position = the_message->curr->position;
    my_off = (the_message->curr->index) * BLK_SIZE;

read_same_block:

    the_block = the_message->curr->elem;
    if (the_block != NULL) // Consistency check
        // compute the actual index of the the block to be read from device
        block_to_read = my_off / BLK_SIZE + 2; // the value 2 accounts for superblock and file-inode on device
    else goto release_token;

    // Consistency check: block_to_read must be less or equal than NBLOCKS + 2
    if (block_to_read > NBLOCKS + 2) goto release_token;

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
        offset = my_off % BLK_SIZE; // Residual bytes
        AUDIT
        {
            printk("%s: Block at index %d has message with length %d", MOD_NAME, get_index(block_to_read), str_len);
            printk("%s: Reading the block at index %d with offset within the block %lld and residual bytes %lld", MOD_NAME, get_index(block_to_read), offset, str_len - offset);
        }
        
        if (offset == 0)
            size = str_len;
        else if (offset < str_len)
            size = str_len - offset;
        else
            size = 0;

        if (len < size) size = len; //If the bytes to read is less than size then copy_to_user will read len bytes
        
        // Copy size bytes of message into the user buffer
        ret = copy_to_user(buf, blk->data + offset, size);

        // Computing the position in the valid messages list to be read
        // I try also to read an eventually inserted block: before the next read begins, a writer may have written other blocks.
        if (ret == 0)
            the_message->position = the_message->curr->position + 1;
        // Unable to copy to the user len bytes
        else
            my_off += size - ret;

        ret = size - ret;
    }
    else
        my_off += BLK_SIZE;

    brelse(bh);

release_token:
    index = (my_epoch & MASK) ? 1 : 0;
    __sync_fetch_and_add(&(sh_data.standing[index]), 1);

    wake_up_interruptible(&invalidate_queue);

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
    AUDIT printk("%s: Device release has been invoked: the thread %d trying to release the device file\n ", MOD_NAME, current->pid);

    if (!mount_md.mounted)
    {
        printk("%s: The device is not mounted", MOD_NAME);
        return -ENODEV;
    }

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
    AUDIT printk("%s: Device open has been invoked: the thread %d trying to open file at offset %lld", MOD_NAME, current->pid, filp->f_pos);

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

    curr->position = INVALID_POSITION;
    filp->private_data = curr;

    return 0;
}



const struct file_operations dev_fops = {
    .owner = THIS_MODULE,
    .read = dev_read,
    .release = dev_release,
    .open = dev_open,
};
