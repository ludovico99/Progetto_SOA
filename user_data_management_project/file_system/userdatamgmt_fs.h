#ifndef _USER_DATA_MANAGEMENT_FS_H
#define _USER_DATA_MANAGEMENT_FS_H

#include "../userdatamgmt.h"

#define MAGIC 0x42424242 //This macro defines a value used to identify user data filesystem's superblock on disk.
#define SB_BLOCK_NUMBER 0 //This macro defines the block number where the user data filesystem's superblock will be stored.
#define DEFAULT_FILE_INODE_BLOCK 1 //This macro defines the block number where the first file inode of the user data filesystem will be stored.
#define FILENAME_MAXLEN 255 //This macro defines the maximum length of a filename.
#define USERDATAFS_ROOT_INODE_NUMBER 10 //This macro defines the inode number for the root directory.
#define USERDATAFS_FILE_INODE_NUMBER 1 //This macro defines the inode number for the first file created.
#define USERDATAFS_INODES_BLOCK_NUMBER 1 //This macro defines the block number where the user data filesystem's inodes are stored.
#define UNIQUE_FILE_NAME "the-file" //This macro defines a unique file name

//inode definition
struct userdatafs_inode {
	mode_t mode;//not exploited
	uint64_t inode_no;
	uint64_t data_block_number;//not exploited

	union {
		uint64_t file_size;
		uint64_t dir_children_count;
	};
};

//dir definition (how the dir datablock is organized)
struct userdatafs_dir_record {
	char filename[FILENAME_MAXLEN];
	uint64_t inode_no;
};


//superblock definition
struct userdatafs_sb_info {
	uint64_t version;
	uint64_t magic;
	uint64_t block_size;
	uint64_t inodes_count;//not exploited
	uint64_t free_blocks;//not exploited

	//padding to fit into a single block
	char padding[ (4 * 1024) - (5 * sizeof(uint64_t))];
};

// file.c
extern const struct inode_operations userdatafs_inode_ops;

// dir.c
extern const struct file_operations userdatafs_dir_operations;

extern int nblocks;

#endif
