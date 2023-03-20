#ifndef _USER_DATA_MANAGEMENT_H
#define _USER_DATA_MANAGEMENT_H

#include <linux/types.h>
#include <linux/fs.h>

#define MOD_NAME "BLOCK-LEVEL DATA MANAGEMENT SERVICE"
#define  AUDIT if(1) //this is a general audit flag
#define BLK_SIZE 4096

#define MAGIC 0x42424242
#define SB_BLOCK_NUMBER 0
#define DEFAULT_FILE_INODE_BLOCK 1
#define FILENAME_MAXLEN 255
#define USERDATAFS_ROOT_INODE_NUMBER 10
#define USERDATAFS_FILE_INODE_NUMBER 1
#define USERDATAFS_INODES_BLOCK_NUMBER 1
#define UNIQUE_FILE_NAME "the-file"

#define EPOCHS (2) //we have the current and the past epoch only
#define MD_SIZE sizeof(unsigned char)
#define SIZE (BLK_SIZE - MD_SIZE)
#define SYNC_FLUSH 
#define NBLOCKS 10
#define PERIOD 100

#define get_index(offset)   ((offset) - 2)
#define get_offset(index)   ((index) + 2)


#define MASK 0x8000000000000000
//#define get_index(my_epoch) (my_epoch & MASK) ? 1 : 0

#define METADATA_MASK 0x80
#define set_valid(i) ((unsigned char)i | METADATA_MASK)
#define set_invalid(i) (unsigned char)i & (~METADATA_MASK)
#define get_validity(i) ((unsigned char)(i) >> (sizeof(unsigned char)*8 - 1))

extern struct blk_rcu_tree **the_tree;
extern struct bdev_metadata bdev_md;
#endif