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
#define MD_SIZE sizeof(uint16_t)
#define SIZE (BLK_SIZE - MD_SIZE)
#define SYNC_FLUSH 
#define NBLOCKS 10
#define PERIOD 1000

#define get_index(offset)   ((offset) - 2)
#define get_offset(index)   ((index) + 2)


#define MASK 0x8000000000000000
//#define get_index(my_epoch) (my_epoch & MASK) ? 1 : 0

#define VALIDITY_MASK 0x8000
#define set_valid(i) (uint16_t)i | (VALIDITY_MASK)
#define set_invalid(i) (uint16_t)i & (~VALIDITY_MASK)
#define get_validity(i) ((uint16_t)(i) >> (sizeof(uint16_t)*8 - 1))

#define FREE_MASK 0x4000
#define set_free(i) (uint16_t)i | (FREE_MASK)
#define set_not_free(i) (uint16_t)i & (~FREE_MASK)
#define get_free(i) ((uint16_t)(i) >> (sizeof(uint16_t)*8 - 2)) & 0x1


#define LEN_MASK 0xF000
#define get_length(i) ((uint16_t)(i) & (~LEN_MASK))
#define set_length(mask,val) ((uint16_t)(mask) & (VALIDITY_MASK | FREE_MASK)) | (((uint16_t)val) & (~LEN_MASK))

extern struct blk_rcu_tree the_tree;
extern struct bdev_metadata bdev_md;
extern struct mount_metadata mount_md;
extern char mount_pt[255];
#endif