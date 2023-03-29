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

#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/namei.h>

#include "userdatamgmt_fs.h"
#include "../userdatamgmt_driver.h"

struct bdev_metadata bdev_md = {0, NULL, NULL};
struct mount_metadata mount_md = {0, "/"};
struct rcu_data sh_data;

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
    if (!sb)
    {
        return -EIO;
    }
    sb_disk = (struct userdatafs_sb_info *)bh->b_data;
    magic = sb_disk->magic;
    brelse(bh);

    bh = sb_bread(sb, USERDATAFS_FILE_INODE_NUMBER);
    if (!sb)
    {
        return -EIO;
    }
    // check on the number of manageable blocks
    inode_disk = (struct userdatafs_inode *)bh->b_data;
    if (NBLOCKS < (inode_disk->file_size / BLK_SIZE))
    {
        return -EINVAL;
    }

    brelse(bh);
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

static void userdatafs_kill_superblock(struct super_block *s)
{
    // wait_queue_head_t unmount_queue;
    DECLARE_WAIT_QUEUE_HEAD(unmount_queue);
    int mnt = -1;
    mnt = __sync_val_compare_and_swap(&mount_md.mounted, 1, 0);
    if (mnt == 0)
    {
        printk("%s: filesystem has been already unmounted\n", MOD_NAME);
        return;
    }

    printk("%s: waiting the pending threads (%d)...", MOD_NAME, bdev_md.count);
    // AUDIT printk("%s: count is %d",MOD_NAME, bdev_md.count);
    wait_event_interruptible(unmount_queue, bdev_md.count == 0);

    kill_block_super(s);

    AUDIT printk("%s: Freeing the struct allocated in the kernel memory", MOD_NAME);
    free_tree(sh_data.head);
    free_list (sh_data.first);
    printk(KERN_INFO "%s: userdatafs unmount succesful.\n", MOD_NAME);
    return;
}

// called on file system mounting
struct dentry *userdatafs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{

    struct dentry *ret;
    int mnt;
    int i = 0;
    int offset;
    struct buffer_head *bh;
    struct blk_element *blk_elem;
    struct message *message;

    mnt = __sync_val_compare_and_swap(&mount_md.mounted, 0, 1);
    if (mnt == 1)
    {
        printk("%s: the device driver can support only a single mount point at time\n", MOD_NAME);
        return ERR_PTR(-EBUSY);
    }

    ret = mount_bdev(fs_type, flags, dev_name, data, userdatafs_fill_super);

    if (unlikely(IS_ERR(ret)))
        printk("%s: error mounting userdatafs", MOD_NAME);
    else
    {
        mount_md.mount_point = mount_pt;
        AUDIT printk("%s: userdatafs is succesfully mounted on from device %s and mount directory %s\n", MOD_NAME, dev_name, mount_md.mount_point);

        // initiliazation of the RCU tree
        // Start filling the block device representation in RAM

        bdev_md.bdev = blkdev_get_by_path(dev_name, FMODE_READ | FMODE_WRITE, NULL);
        bdev_md.path = dev_name;
        init(&sh_data);
        // Initialization of struct blk_metadata
        for (i = 0; i < NBLOCKS; i++)
        {

            offset = get_offset(i);
            bh = (struct buffer_head *)sb_bread((bdev_md.bdev)->bd_super, offset);
            if (!bh)
            {
                printk("%s: Error retrieving the block at offset %d\n", MOD_NAME, offset);
                return ERR_PTR(-EIO);
            }
            if (bh->b_data != NULL)
            {
                // AUDIT printk("%s: retrieved the block at offset %d (index %d)\n", MOD_NAME, offset, i);
                blk_elem = (struct blk_element *)kzalloc(sizeof(struct blk_element), GFP_KERNEL);
                if (!blk_elem)
                {
                    printk("%s: Error allocationg blk_element struct\n", MOD_NAME);
                    return ERR_PTR(-ENOMEM);
                }
                blk_elem->metadata = ((struct dev_blk *)bh->b_data)->metadata;
                blk_elem->index = i;
                 // AUDIT printk("%s: Block at offset %d (index %d) is %d (1 stays for valid) contains the message = %s\n", MOD_NAME, offset, i, get_validity(blk_elem->blk->metadata), blk_elem->blk->data);
                tree_insert(&sh_data.head, blk_elem);

                if (get_validity(((struct dev_blk *)bh->b_data)->metadata))
                {
                    message = (struct message *)kzalloc(sizeof(struct message), GFP_KERNEL);
                    if (!message)
                    {
                        printk("%s: Error allocationg message struct\n", MOD_NAME);
                        return ERR_PTR(-ENOMEM);
                    }
                    message->elem = blk_elem;
                    blk_elem -> msg = message;
                    insert(&sh_data.first,&sh_data.last, message);
                }     
            }
            brelse(bh);
        }
    }
    return ret;
}

// file system structure
static struct file_system_type userdatafs_type = {
    .owner = THIS_MODULE,
    .name = "userdatafs",
    .mount = userdatafs_mount,
    .kill_sb = userdatafs_kill_superblock,
};
