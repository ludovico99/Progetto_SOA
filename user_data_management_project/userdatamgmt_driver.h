#ifndef _USER_DATA_MANAGEMENT_DRIVER_H
#define _USER_DATA_MANAGEMENT_DRIVER_H

#include <linux/spinlock.h>
#include "userdatamgmt.h"


// user_data_management_driver.c
extern long sys_put_data;
extern long sys_get_data;
extern long sys_invalidate_data;

// userdatamgmt_driver.c
extern const struct file_operations dev_fops;

/* kernel data structures that allow RCU management of the block device:
    -blk_metadata: contains the metadata for a block in the device
    -blk_element: represent an element in the RCU list
    -bdev_metadata: contains the metadata for the block device
    -blk_rcu_list: contains the metadata to implement an RCU approach. It contains blk_element
*/

typedef struct _blk_metadata{
	unsigned int dirtiness; //if 1 the changes should be flushed into the device
    int index;
} blk_metadata;

typedef struct _blk_element{
	struct _blk_list * next __attribute__((aligned(64)));
    blk_metadata * metadata __attribute__((aligned(64)));
    char data[SIZE];
} blk_element;

typedef struct _bdev_metadata{
    struct block_device *bdev;
    const char *path; //path to the block device to open
} bdev_metadata;

typedef struct _blk_rcu_list{
	unsigned long standing[EPOCHS] __attribute__((aligned(64))); // In memory alignment to reduce CC transactions
	unsigned long epoch __attribute__((aligned(64))); // In memory alignment to reduce CC transactions
	int next_epoch_index; // index of the next epoch
	spinlock_t  write_lock; // write lock to be acquired in order to modify the shared data structure
	blk_element * head; // pointer to the shared data structure
    bdev_metadata bdev_md __attribute__((aligned(64))); //Metadata of the block device
} __attribute__((packed)) blk_rcu_list;

typedef blk_rcu_list list __attribute__((aligned(64)));


#endif
