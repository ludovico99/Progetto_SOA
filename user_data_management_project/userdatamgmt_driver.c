
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

    // check that *off is within boundaries
    if (my_off >= file_size)
    {
        return 0;
    }

    my_epoch = __sync_fetch_and_add(&(sh_data.epoch), 1);

    offset = my_off % BLK_SIZE; // Residual

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
    // Reading the current block
    else if (offset != 0 && offset < MD_SIZE + str_len)   
    {
        the_block = the_message->curr -> elem;
        // compute the actual index of the the block to be read from device
        block_to_read = my_off / BLK_SIZE + 2; // the value 2 accounts for superblock and file-inode on device
    }
    // Reading the following block in the valid message list
    else {
        if (the_message->curr->next == NULL) {
            index = (my_epoch & MASK) ? 1 : 0;
            __sync_fetch_and_add(&(sh_data.standing[index]), 1);
            return 0; // Read operation succesfully concluded
        }
        the_block = the_message->curr->next->elem;
        block_to_read = get_offset(the_block->index);
        offset = 0;
    }
   

    the_message->curr = the_block -> msg;
    the_message->index = get_index(block_to_read);

    if (block_to_read > NBLOCKS + 2)
    { // Consistency check
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

        AUDIT printk("%s: Reading the block at index %d with offset within the block %lld and residual bytes %lld", MOD_NAME, get_index(block_to_read), offset, str_len - offset);

        if (offset == 0)
            len = str_len;
        else if (offset < MD_SIZE + str_len)
            len = str_len - offset;
        else
            len = 0;

        ret = copy_to_user(buf, blk->data + offset, len);
        if (ret == 0) my_off += offset + len;

        else
            my_off += len - ret;
    }
    else
        my_off += BLK_SIZE;

    brelse(bh);
    
    index = (my_epoch & MASK) ? 1 : 0;
    __sync_fetch_and_add(&(sh_data.standing[index]), 1);

    *off = my_off;
    the_message->offset = my_off;

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

    curr->index = -1;
    filp->private_data = curr;

    return 0;
}

const struct file_operations dev_fops = {
    .owner = THIS_MODULE,
    .read = dev_read,
    .release = dev_release,
    .open = dev_open,
};
