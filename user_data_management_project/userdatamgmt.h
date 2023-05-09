#ifndef _USER_DATA_MANAGEMENT_H
#define _USER_DATA_MANAGEMENT_H

#include <linux/types.h>
#include <linux/fs.h>

#define MOD_NAME "BLOCK-LEVEL DATA MANAGEMENT SERVICE" //This is a macro that defines the name of the module as a string.
#define  AUDIT if(0) //This is a debugging macro.
#define BLK_SIZE 4096 //This macro defines the block size in bytes.

#define EPOCHS (2) //This macro defines the number of times an operation can be performed before it has to wait for next epoch.
#define POS_SIZE sizeof(unsigned int)
#define MD_SIZE (sizeof(uint16_t) + POS_SIZE) //This macro defines the size of the metadata (in bytes).
#define SIZE (BLK_SIZE - MD_SIZE) //This macro defines the maximum size of user data (in bytes).
#define SYNC_FLUSH 
#define NBLOCKS 10000 //This macro defines the number of blocks manageable by the device driver.
#define PERIOD 10000

#define get_index(offset)   ((offset) - 2) //This macro retrieves the index from an offset value.
#define get_offset(index)   ((index) + 2) //This macro retrieves the offset from an index value.

#define MASK 0x8000000000000000

#define VALIDITY_MASK 0x8000 //This macro defines a bitwise mask for validity flag.
#define set_valid(i) ((uint16_t)i | (VALIDITY_MASK)) //This macro sets the validity flag of a given value.
#define set_invalid(i) ((uint16_t)i & (~VALIDITY_MASK)) //This macro clears the validity flag of a given value.
#define get_validity(i) ((uint16_t)(i) >> (sizeof(uint16_t)*8 - 1)) //This macro retrieves the validity flag from a given value.

//The free bit is not exploited at all
#define FREE_MASK 0x4000 //This macro defines a bitwise mask for free flag.
#define set_free(i) ((uint16_t)i | (FREE_MASK)) //This macro sets the free flag of a given value.
#define set_not_free(i) ((uint16_t)i & (~FREE_MASK)) //This macro clears the free flag of a given value.
#define get_free(i) ((uint16_t)(i) >> (sizeof(uint16_t)*8 - 2)) & 0x1 //This macro retrieves the free flag from a given value.


#define LEN_MASK 0xF000 //This macro defines a bitwise mask for length field.
#define get_length(i) ((uint16_t)(i) & (~LEN_MASK)) //This macro retrieves the length field from a given value.
#define set_length(mask,val) (((uint16_t)(mask) & (VALIDITY_MASK | FREE_MASK)) | (((uint16_t)val) & (~LEN_MASK))) //This macro sets the length field of a given value.

#define INVALID (~0xFFFFFFFF) //This macro defines the value used to express an invalid position in a block

extern struct rcu_data rcu;
extern struct bdev_metadata bdev_md;
extern struct mount_metadata mount_md;
extern char mount_pt[255];

#define is_after(a,b)		\
	 ((int)((b) - (a)) < 0)
#define is_before(a,b)	is_after(b,a)
#define is_after_eq(a,b)	\
	 (int)((a) - (b)) >= 0
#define is_before_eq(a,b) is_after_eq(b,a)

#endif