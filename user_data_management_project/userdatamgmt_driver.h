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
    -dev_blk: logical representation of a block in the device
    -blk_element: represent an element in the RCU list
    -bdev_metadata: contains the metadata for the block device
    -mount metadata: contains the metadata for the mount operation
    -blk_rcu_tree: contains the metadata to implement an RCU approach. Its made up of blk_element
*/

struct blk_element
{
    struct blk_element *right;
    struct blk_element *left;
    uint16_t metadata;
    int index;
    unsigned int dirtiness; // if 1 the changes should be flushed into the device
    //struct dev_blk *dev_blk;
};

struct dev_blk
{   
    uint16_t metadata;
    char data[SIZE];
};

struct bdev_metadata
{   
    unsigned int count __attribute__((aligned(64)));
    struct block_device *bdev;
    const char *path; // path to the block device to open
}__attribute__((aligned(64)));

struct mount_metadata
{   
    int mounted;
    char *mount_point; 
}__attribute__((aligned(64)));

struct blk_rcu_tree
{   
    struct blk_element *head;                                    // pointer to the shared data structure
    unsigned long standing[EPOCHS] __attribute__((aligned(64))); // In memory alignment to reduce CC transactions
    unsigned long epoch __attribute__((aligned(64)));            // In memory alignment to reduce CC transactions
    int next_epoch_index;                                        // index of the next epoch
    spinlock_t write_lock;                                       // write lock to be acquired in order to modify the shared data structure
} __attribute__((aligned(64)));

extern void rcu_tree_init(struct blk_rcu_tree *);
extern struct blk_element *lookup(struct blk_element *, int);
extern void insert(struct blk_element **, struct blk_element *);
extern void stampa_albero(struct blk_element *root);
extern void free_tree(struct blk_element *);
extern struct blk_element* delete(struct blk_element*, int);
extern struct blk_element* inorderTraversal(struct blk_element*);

#endif
