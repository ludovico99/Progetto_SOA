#define EXPORT_SYMTAB

struct bdev_metadata bdev_md = {0, NULL, NULL};
struct mount_metadata mount_md = {0, "/"};
struct rcu_data sh_data;

int nblocks = 0; //Real number of blocks into the device

struct blk_element **head; //It's the array of block's metadata

static struct super_operations my_super_ops = {};

static struct dentry_operations my_dentry_ops = {};

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

    bh = sb_bread(sb, USERDATAFS_FILE_INODE_NUMBER);
    if (!bh)
    {
        return -EIO;
    }
    // check on the number of manageable blocks
    inode_disk = (struct userdatafs_inode *)bh->b_data;
    nblocks = inode_disk->file_size / BLK_SIZE;
    if (NBLOCKS < nblocks)
    {
        printk("%s: Too many block to manage", MOD_NAME);
        brelse(bh);
        return -EINVAL;
    }
   

    // check on the expected magic number
    if (magic != sb->s_magic)
    {
        return -EBADF;
    }

    sb->s_fs_info = NULL;     // FS specific data (the magic number) already reported into the generic superblock
    sb->s_op = &my_super_ops; // set our own operations

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
    DECLARE_WAIT_QUEUE_HEAD(unmount_queue); // This variable is a wait queue for threads that are waiting for unmount to complete.
    int mnt = -1;
    int offset = 0;
    int i = 0;
    unsigned int pos = 0;
    struct buffer_head *bh;
    struct dev_blk *the_block;
    struct message *curr = sh_data.first;
    /*fetches the value of the mounted flag using an atomic compare-and-swap operation, setting the flag to 0 if it was previously 1.
    If the mounted flag was already 0, it prints an error message and returns.*/
    mnt = __sync_val_compare_and_swap(&mount_md.mounted, 1, 0);
    if (mnt == 0)
    {
        printk("%s: filesystem has been already unmounted\n", MOD_NAME);
        return;
    }

    AUDIT printk("%s: waiting the pending threads (%d)...", MOD_NAME, bdev_md.count);
    /*puts the current process to sleep until the condition (bdev_md.count == 0) is true or an interrupt is received.*/
    wait_event_interruptible(unmount_queue, bdev_md.count == 0);

    AUDIT printk("%s: Flushing to the device ordering of user messages ...", MOD_NAME);

    while (curr != NULL)
    {
        offset = get_offset(curr->index);

        bh = (struct buffer_head *)sb_bread((bdev_md.bdev)->bd_super, offset);
        if (!bh)
        {
            printk("%s: Error retrieving the block at offset %d\n", MOD_NAME, offset);
            break;
        }
        the_block = (struct dev_blk *)bh->b_data;
        if (the_block != NULL)
        {
            the_block->pos = pos;
        }
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
        brelse(bh);
        pos++;
        curr = curr->next;
    }
    blkdev_put(bdev_md.bdev, FMODE_READ | FMODE_WRITE);
    /*After all pending threads have finished, it calls kill_block_super() to release resources associated with the superblock.*/
    kill_block_super(s);

    AUDIT printk("%s: Freeing the struct allocated in the kernel memory", MOD_NAME);

    /* It then frees the memory allocated for the array and sorted list using the kfree (or vmalloc) and free_list() functions, respectively.*/
    for (i = 0; i < nblocks; i++){
        kfree(head[i]);
    }
    //Freeing the head of the array...
    if (sizeof(struct blk_element *) * nblocks < 128 * 1024) kfree(head);
    else vfree(head);

    //Freeing the double linked list of valid messages...
    free_list(sh_data.first);
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
    int index;
    struct buffer_head *bh;
    struct dev_blk *the_block;
    struct message *message;
    struct dentry *ret;
    int i = 0;
    int mnt;

    //If the CAS operation is able to change the value 0 to 1, it means that the device driver has not been mounted before
    mnt = __sync_val_compare_and_swap(&mount_md.mounted, 0, 1);
    if (mnt == 1)
    {
        printk("%s: the device driver can support only a single mount point at time\n", MOD_NAME);
        return ERR_PTR(-EBUSY);
    }
    //Calls the mount_bdev() function to mount the specified block device onto the filesystem.
    ret = mount_bdev(fs_type, flags, dev_name, data, userdatafs_fill_super);
    if (unlikely(IS_ERR(ret)))
        printk("%s: error mounting userdatafs", MOD_NAME);
    else
    { 
        // If the mount operation is successful, then it sets the variables used to implement RCU approach and initializes the lock involved.
        mount_md.mount_point = mount_pt;
        AUDIT printk("%s: userdatafs is succesfully mounted on from device %s and mount directory %s\n", MOD_NAME, dev_name, mount_md.mount_point);

        bdev_md.bdev = blkdev_get_by_path(dev_name, FMODE_READ | FMODE_WRITE, NULL);
        bdev_md.path = dev_name;
        // Initialization of the RCU data
        init(&sh_data);
       
        // Allocates memory to the 'head' using kzalloc or vmalloc based on the size of 'unsigned int * NBLOCKS'
        if (sizeof(struct blk_element *) * nblocks < 128 * 1024)
            head = (struct blk_element **)kzalloc(sizeof(struct blk_element *) * nblocks, GFP_KERNEL);
        else
            head= (struct blk_element **)vmalloc(sizeof(struct blk_element *) * nblocks);

        if (!head)
        {
            printk("%s: Error allocationg blk_element array\n", MOD_NAME);
            blkdev_put(bdev_md.bdev, FMODE_READ | FMODE_WRITE);
            ret = ERR_PTR(-ENOMEM);
            goto exit;
        }
        // Creating the array and the "overlayed" list that contains all valid messages
        for (i = 0; i < nblocks; i++)
        { /*Loops over all blocks in the block device, reads each block from the block device and checks whether it is a valid message or not by reading its metadata.
          It then inserts it into an array and sorted list based on its index*/

            offset = get_offset(i);
            bh = (struct buffer_head *)sb_bread((bdev_md.bdev)->bd_super, offset);
            if (!bh)
            {
                printk("%s: Error retrieving the block at offset %d\n", MOD_NAME, offset);
                ret = ERR_PTR(-EIO);
                blkdev_put(bdev_md.bdev, FMODE_READ | FMODE_WRITE);
                if (sizeof(struct blk_element*) * nblocks < 128 * 1024)
                    kfree(*head);
                else
                    vfree(*head);
                goto exit;
            }
            the_block = (struct dev_blk *)bh->b_data;
            if (the_block != NULL)
            {   
                //Allocating the struct that represents the metadata (and other stuffs) for the block at offset (i + 2)
                head[i] = (struct blk_element*) kzalloc(sizeof(struct blk_element), GFP_KERNEL);
                if (!head)
                {
                    printk("%s: Error allocationg blk_element struct\n", MOD_NAME);
                    ret = ERR_PTR(-ENOMEM);
                    goto exit;
                }

                //Link the metadata in position i of the corresponding list
                head[i]->metadata = the_block->metadata;

                AUDIT printk("%s: Block at offset %d (index %d), at position %d, contains the message = %s\n", MOD_NAME, offset, index, the_block->pos, the_block->data);

                if (get_validity(the_block->metadata)) //The block at position i is valid. it has to be inserted in the double linked list
                {
                    message = (struct message *)kzalloc(sizeof(struct message), GFP_KERNEL);
                    if (!message)
                    {
                        printk("%s: Error allocationg message struct\n", MOD_NAME);
                        blkdev_put(bdev_md.bdev, FMODE_READ | FMODE_WRITE);
                        if (sizeof(struct blk_element *) * nblocks < 128 * 1024)
                            kfree(*head);
                        else
                            vfree(*head);
                        brelse(bh);
                        ret = ERR_PTR(-ENOMEM);
                        goto exit;
                    }

                    //Initialization of the fields of the message structure
                    message->elem = head[i];
                    head[i]->msg = message;
                    message -> index = i;
                    message->position = the_block->pos;
                    
                    // Insertion in the valid messages double linked list
                    insert_sorted(&sh_data.first, &sh_data.last, message, the_block->pos);
                }
            }
            brelse(bh);
        }

    exit:
        if (unlikely(IS_ERR(ret)))
            mount_md.mounted = 0;
    }
    /*Finally, the function returns a pointer to the dentry structure if successful or an error pointer if unsuccessful.*/
    return ret;
}

// file system structure
static struct file_system_type userdatafs_type = {
    .owner = THIS_MODULE,
    .name = "userdatafs",
    .mount = userdatafs_mount,
    .kill_sb = userdatafs_kill_superblock};
