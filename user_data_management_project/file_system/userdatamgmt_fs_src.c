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

#include "userdatamgmt_fs.h"

static struct super_operations my_super_ops = {
};


static struct dentry_operations my_dentry_ops = {
};



int userdatafs_fill_super(struct super_block *sb, void *data, int silent) {   

    struct inode *root_inode;
    struct buffer_head *bh;
    struct userdatafs_sb_info *sb_disk;
    struct timespec64 curr_time;
    uint64_t magic;


    //Unique identifier of the filesystem
    sb->s_magic = MAGIC;

    bh = sb_bread(sb, SB_BLOCK_NUMBER);
    if(!sb){
	return -EIO;
    }
    sb_disk = (struct userdatafs_sb_info *)bh->b_data;
    magic = sb_disk->magic;
    brelse(bh);

    //check on the expected magic number
    if(magic != sb->s_magic){
	return -EBADF;
    }

    sb->s_fs_info = NULL; //FS specific data (the magic number) already reported into the generic superblock
    sb->s_op = &my_super_ops;//set our own operations


    root_inode = iget_locked(sb, 0);//get a root inode indexed with 0 from cache
    if (!root_inode){
        return -ENOMEM;
    }

    root_inode->i_ino = USERDATAFS_ROOT_INODE_NUMBER;//this is actually 10
    inode_init_owner(&init_user_ns, root_inode, NULL, S_IFDIR);//set the root user as owned of the FS root
    root_inode->i_sb = sb;
    root_inode->i_op = &userdatafs_inode_ops;//set our inode operations
    root_inode->i_fop = &userdatafs_dir_operations;//set our file operations
    //update access permission
    root_inode->i_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;

    //baseline alignment of the FS timestamp to the current time
    ktime_get_real_ts64(&curr_time);
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = curr_time;

    // no inode from device is needed - the root of our file system is an in memory object
    root_inode->i_private = NULL;

    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root)
        return -ENOMEM;

    sb->s_root->d_op = &my_dentry_ops;//set our dentry operations

    //unlock the inode to make it usable
    unlock_new_inode(root_inode);

    return 0;
}

static void userdatafs_kill_superblock(struct super_block *s) {
    kill_block_super(s);
    printk(KERN_INFO "%s: userdatafs unmount succesful.\n",MOD_NAME);
    return;
}

//called on file system mounting 
struct dentry *userdatafs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {

    struct dentry *ret;

    ret = mount_bdev(fs_type, flags, dev_name, data, userdatafs_fill_super);

    if (unlikely(IS_ERR(ret)))
        printk("%s: error mounting userdatafs",MOD_NAME);
    else
        printk("%s: userdatafs is succesfully mounted on from device %s\n",MOD_NAME,dev_name);

    return ret;
}

//file system structure
static struct file_system_type userdatafs_type = {
	.owner = THIS_MODULE,
        .name           = "userdatafs",
        .mount          = userdatafs_mount,
        .kill_sb        = userdatafs_kill_superblock,
};