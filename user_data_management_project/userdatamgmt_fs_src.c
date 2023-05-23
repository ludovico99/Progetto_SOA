#define EXPORT_SYMTAB

struct bdev_metadata  __attribute__((aligned(64))) bdev_md = {0, NULL, NULL};
struct mount_metadata  __attribute__((aligned(64))) mount_md = {false, "/"};
struct rcu_data  __attribute__((aligned(64))) rcu;
struct blk_element **head; // It's the array of block's metadata

static struct super_operations my_super_ops = {};
static struct dentry_operations my_dentry_ops = {};

struct task_struct *the_daemon = NULL;

struct device_info  __attribute__((aligned(64))) dev_info = {0,0,0};



DECLARE_WAIT_QUEUE_HEAD(unmount_queue); // This variable is a wait queue for threads that are still executing.

int userdatafs_fill_super(struct super_block *sb, void *data, int silent)
{

    struct inode *root_inode;
    struct buffer_head *bh;
    struct userdatafs_sb_info *sb_disk;
    struct userdatafs_inode *inode_disk;
    struct timespec64 curr_time;
    uint64_t magic;

    // Unique identifier of the filesystem
    sb->s_magic = MAGIC;

    bh = sb_bread(sb, SB_BLOCK_NUMBER);
    if (!bh)
    {
        return -EIO;
    }
    sb_disk = (struct userdatafs_sb_info *)bh->b_data;
    magic = sb_disk->magic;
    brelse(bh);

    // check on the expected magic number
    if (magic != sb->s_magic)
    {
        return -EBADF;
    }

    sb->s_fs_info = NULL;     // FS specific data (the magic number) already reported into the generic superblock
    sb->s_op = &my_super_ops; // set our own operations

    bh = sb_bread(sb, USERDATAFS_FILE_INODE_NUMBER);
    if (!bh)
    {
        return -EIO;
    }
    // check on the number of manageable blocks
    inode_disk = (struct userdatafs_inode *)bh->b_data;

    dev_info.device_size = inode_disk->file_size;
    dev_info.nblocks = inode_disk->file_size / BLK_SIZE; // Computing the number of block in the device

    printk("%s: number of block in the device is %d", MOD_NAME, dev_info.nblocks);
    brelse(bh);
    if (NBLOCKS < dev_info.nblocks)
    {
        printk("%s: Too many block to manage", MOD_NAME);
        return -EINVAL;
    }

    root_inode = iget_locked(sb, 0); // get a root inode indexed with 0 from cache
    if (!root_inode)
    {
        return -ENOMEM;
    }

    root_inode->i_ino = USERDATAFS_ROOT_INODE_NUMBER; // this is actually 10
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)
    inode_init_owner(&init_user_ns, root_inode, NULL, S_IFDIR); // set the root user as owned of the FS root
#else
    inode_init_owner(root_inode, NULL, S_IFDIR); // set the root user as owned of the FS root
#endif

    root_inode->i_sb = sb;
    root_inode->i_op = &userdatafs_inode_ops;       // set our inode operations
    root_inode->i_fop = &userdatafs_dir_operations; // set our file operations
    // update access permission
    root_inode->i_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;

    // baseline alignment of the FS timestamp to the current time
    ktime_get_real_ts64(&curr_time);
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = curr_time;

    // no inode from device is needed - the root of our file system is an in memory object
    root_inode->i_private = NULL;

    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root)
        return -ENOMEM;

    sb->s_root->d_op = &my_dentry_ops; // set our dentry operations

    // unlock the inode to make it usable
    unlock_new_inode(root_inode);
    return 0;
}

/*This function is used to unmount the userdatafs filesystem.

Arguments:
s
This argument is a pointer to the super_block structure.*/
static void userdatafs_kill_superblock(struct super_block *s)
{
    int mnt = -1, i = 0;
    unsigned int pos = 0;
    int offset = 0;
    uint16_t the_metadata = 0;
    struct buffer_head *bh;
    struct dev_blk *the_block;
    struct message *curr = rcu.first;

    /*fetches the value of the mounted flag using an atomic compare-and-swap operation, setting the flag to 0 if it was previously 1.
    If the mounted flag was already 0, it prints an error message and returns.*/
    mnt = __sync_val_compare_and_swap(&mount_md.mounted, true, false);
    if (mnt == 0)
    {
        printk("%s: filesystem has been already unmounted\n", MOD_NAME);
        return;
    }

    AUDIT printk("%s: waiting the pending threads (%d)...", MOD_NAME, bdev_md.count);
    /*puts the current process to sleep until the condition (bdev_md.count == 0) is true or an interrupt is received.*/
    wait_event_interruptible(unmount_queue, bdev_md.count == 0);

    if (head == NULL)
    {
        printk("%s: head of the array of metadata is null ...", MOD_NAME);
        goto exit;
    }

    AUDIT printk("%s: Flushing to the device some changes...", MOD_NAME);

    // The insert_index is normalized to the position in the valid messages list
    while (curr != NULL)
    {
        pos++;
        curr->ordering.position = pos;
        curr = curr->next;
    }

    for (i = 0; i < dev_info.nblocks; i++)
    {
        offset = get_offset(i);

        bh = (struct buffer_head *)sb_bread((bdev_md.bdev)->bd_super, offset);
        if (!bh)
        {
            printk("%s: Error retrieving the block at offset %d\n", MOD_NAME, offset);
            break;
        }
        the_block = (struct dev_blk *)bh->b_data;
        if (the_block != NULL)
        { // If the block is valid it flushes the position in the device
            if (get_validity(head[i]->metadata))
                the_block->position = head[i]->msg->ordering.position;

            // If the block is valid it updates the metadata and set the position to -1 in the device
            else
            {
                the_metadata = set_invalid(the_block->metadata);
                memcpy(&(the_block->metadata), &the_metadata, MD_SIZE - POS_SIZE);
                the_block->position = INVALID;
            }

        }

        // Sets the dirty bit of the bh structure and marks it as requiring writeback.
        mark_buffer_dirty(bh);

        brelse(bh);
    }

exit:
    if (bdev_md.bdev != NULL)
        blkdev_put(bdev_md.bdev, FMODE_READ | FMODE_WRITE);

    /*After all pending threads have finished, it calls kill_block_super() to release resources associated with the superblock.*/
    if (s != NULL)
        kill_block_super(s);

    AUDIT printk("%s: Freeing the struct allocated in the kernel memory", MOD_NAME);

    if (head != NULL)
        free_array(head);

    // Freeing the double linked list of valid messages...
    if (rcu.first != NULL)
        free_list(rcu.first);

    wake_up_interruptible(&wait_queue);
    if (the_daemon != NULL) {
        AUDIT printk("%s: Stopping the kernel daemon ...", MOD_NAME);
        kthread_stop(the_daemon);
        the_daemon = NULL;
    }

    // Finally, it prints a log message indicating that unmount was successful and returns.
    printk(KERN_INFO "%s: userdatafs unmount succesful.\n", MOD_NAME);
    return;
}

/*This function is used to mount the userdatafs filesystem. It takes four arguments and returns a pointer to the dentry structure on success (or an error pointer on failure).
Arguments:
fs_type
This argument is a pointer to the file system type.

flags
This argument is the flags that determine how the filesystem should be mounted.

dev_name
This argument is the name of the block device file that the filesystem is mounted on.

data
This argument is the data passed by the user when they call mount().*/
struct dentry *userdatafs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    int offset;
    struct buffer_head *bh = NULL;
    struct dev_blk *the_block = NULL;
    struct message *message = NULL;
    struct dentry *ret = NULL;
    int i = 0;
    int mnt;

    // If the CAS operation is able to change the value 0 to 1, it means that the device driver has not been mounted before
    mnt = __sync_val_compare_and_swap(&mount_md.mounted, false, true);
    if (mnt == 1)
    {
        printk("%s: the device driver can support only a single mount point at time\n", MOD_NAME);
        return ERR_PTR(-EBUSY);
    }
    // Calls the mount_bdev() function to mount the specified block device onto the filesystem.
    ret = mount_bdev(fs_type, flags, dev_name, data, userdatafs_fill_super);
    if (unlikely(IS_ERR(ret)))
        printk("%s: error mounting userdatafs", MOD_NAME);
    else
    {
        // If the mount operation is successful, then it sets the variables used to implement RCU approach and initializes the lock involved.
        AUDIT printk("%s: userdatafs is succesfully mounted on, from device %s\n", MOD_NAME, dev_name);

        bdev_md.bdev = blkdev_get_by_path(dev_name, FMODE_READ | FMODE_WRITE, NULL);
        bdev_md.path = dev_name;
        // Initialization of the RCU data
        init(&rcu);

        // Allocates memory to the 'head' using kzalloc or vmalloc based on the size of 'unsigned int * NBLOCKS'
        if (sizeof(struct blk_element *) * dev_info.nblocks < 128 * 1024)
            head = (struct blk_element **)kzalloc(sizeof(struct blk_element *) * dev_info.nblocks, GFP_KERNEL);
        else
            head = (struct blk_element **)vmalloc(sizeof(struct blk_element *) * dev_info.nblocks);

        if (!head)
        {
            printk("%s: Error allocationg blk_element array\n", MOD_NAME);
            ret = ERR_PTR(-ENOMEM);
            goto exit_2;
        }
        // Creating the array and the "overlayed" list that contains all valid messages
        for (i = 0; i < dev_info.nblocks; i++)
        { /*Loops over all blocks in the block device, reads each block from the block device and checks whether it is a valid message or not by reading its metadata.
          It then inserts it into an array and sorted list based on its index*/

            offset = get_offset(i);
            bh = (struct buffer_head *)sb_bread((bdev_md.bdev)->bd_super, offset);
            if (!bh)
            {
                printk("%s: Error retrieving the block at offset %d\n", MOD_NAME, offset);
                ret = ERR_PTR(-EIO);
                goto exit_2;
            }
            the_block = (struct dev_blk *)bh->b_data;
            if (the_block != NULL)
            {
                // Allocating the struct that represents the metadata (and other stuffs) for the block at offset (i + 2)
                head[i] = (struct blk_element *)kzalloc(sizeof(struct blk_element), GFP_KERNEL);
                if (!head)
                {
                    printk("%s: Error allocationg blk_element struct\n", MOD_NAME);
                    brelse(bh);
                    ret = ERR_PTR(-ENOMEM);
                    goto exit_2;
                }

                // Link the metadata in position i of the corresponding list
                head[i]->metadata = the_block->metadata;

                AUDIT printk("%s: Block at offset %d (index %d), with insert_index %d, contains the message = %s\n", MOD_NAME, offset, i, the_block->position, the_block->data);

                if (get_validity(the_block->metadata)) // The block at position i is valid. it has to be inserted in the double linked list
                {
                    message = (struct message *)kzalloc(sizeof(struct message), GFP_KERNEL);
                    if (!message)
                    {
                        printk("%s: Error allocationg message struct\n", MOD_NAME);
                        brelse(bh);
                        ret = ERR_PTR(-ENOMEM);
                        goto exit_2;
                    }

                    // Initialization of the fields of the message structure
                    message->elem = head[i];
                    head[i]->msg = message;
                    message->index = i;
                    message->ordering.position = the_block->position;

                    // Insertion ordered in the valid messages double linked list
                    insert_sorted(&rcu.first, &rcu.last, message);

                    /*decomment the previous line in case you want to use the Quick Sort*/
                    // insert(&rcu.first, &rcu.last, message);
                }
            }
            brelse(bh);
        }
        /*Quick sort has been implemented for performance reasons. To avoid too many recursions is commented and
        the iterative version with upper bound O(n^2) is preferred. */
        // quickSort(rcu.first, rcu.last); // Sorting with upper bound O(n*log(n))
        if (rcu.last != NULL)
            dev_info.num_insertions = rcu.last->ordering.position; // Set the value of total number of insertions to the number of valid messages...

    exit_2:
        if (unlikely(IS_ERR(ret)))
        {
            if (head != NULL)
                free_array(head); // freeing the array of metadata
            if (rcu.first != NULL)
                free_list(rcu.first);                           // freeing the double linked list
            blkdev_put(bdev_md.bdev, FMODE_READ | FMODE_WRITE); // blkdev_get_by_path has been invoked
        }
    }

    if (unlikely(IS_ERR(ret)))
        mount_md.mounted = false;

    /*Finally, the function returns a pointer to the dentry structure if successful or an error pointer if unsuccessful.*/
    return ret;
}

// file system structure
static struct file_system_type userdatafs_type = {
    .owner = THIS_MODULE,
    .name = "userdatafs",
    .mount = userdatafs_mount,
    .kill_sb = userdatafs_kill_superblock};
