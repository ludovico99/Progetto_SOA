#ifndef _USER_DATA_MANAGEMENT_DRIVER_H
#define _USER_DATA_MANAGEMENT_DRIVER_H

#include <linux/spinlock.h>
#include "userdatamgmt.h"
#include <linux/version.h>
// user_data_management_driver.c
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
extern long sys_put_data; //This variable contains the pointer to the system call for putting data.
extern long sys_get_data; //This variable contains the pointer to the system call for getting data.
extern long sys_invalidate_data; //This variable contains the pointer to the system call for invalidating data.
#else
#endif

// userdatamgmt_driver.c
extern const struct file_operations dev_fops; //This variable contains the file operation structures for the block device driver.

extern struct blk_element **head;  //Pointer to the shared data structure that contains block metadata

/*This struct represents an element in an array that contains the block metadata.*/
struct blk_element
{
    struct message * msg;
    uint16_t metadata;

};
/*This struct contains a pointer to a blk_element, pointers to the next and previous messages in the list, 
the index in the block device and the position in the list of valid messages.*/
struct message
{   
    struct blk_element *elem;
    struct message *next;
    struct message *prev;
    int index;
    int position;

};
/*This struct contains the position, message and offset of the current message being processed in the dev_read*/
struct current_message {
    int position;
    struct message * curr;
    loff_t offset;
};

/*This struct represents the memory representation of a block in the device. It contains metadata (uint16_t), the position in the valid message double linked list (pos) and data (char array).*/
struct dev_blk
{   
    char data[SIZE];
    uint16_t metadata;
    int pos;

};

//This struct contains the count, block_device pointer and path string of the block device.
struct bdev_metadata
{   
    unsigned int count;
    struct block_device *bdev;
    const char *path; // path to the block device to open
}__attribute__((aligned(64)));

//This struct contains mounted flag and mount_point string.
struct mount_metadata
{   
    int mounted;
    char *mount_point; 
}__attribute__((aligned(64)));

/*This struct contains the head pointer to blk_element list, first and last pointers to message list, standing (an array of integers representing epochs), epoch (the current epoch), next_epoch_index (the index of the next epoch), and write_lock (a spinlock used to acquire the lock before modifying the shared data structure). 
All of these variables are used to implement RCU approach.*/
struct rcu_data
{   
    struct message *first;                                     // pointer to the shared data structure that represent a list that is ordered following the message insertion 
    struct message *last; 
    unsigned long standing[EPOCHS] __attribute__((aligned(64))); // In memory alignment to reduce CC transactions
    unsigned long epoch __attribute__((aligned(64)));            // In memory alignment to reduce CC transactions
    int next_epoch_index;                                        // index of the next epoch
    spinlock_t write_lock;                                       // write lock to be acquired in order to modify the shared data structure
} __attribute__((aligned(64)));

#endif
