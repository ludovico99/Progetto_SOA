
obj-m += progetto_soa.o 
progetto_soa-objs += userdatamgmt.o file.o dir.o utils.o lib/scth.o

NBLOCKS = 10000
TOT_BLOCKS = $(shell expr $(NBLOCKS) + 2)
#MOUNT_DIR = /home/ludovico99/Scrivania/Progetto_SOA/user_data_management_project/mount/

SYSCALL_TABLE_ADDRESS = $(shell cat /sys/module/the_usctm/parameters/sys_call_table_address)
SYS_NI_SYSCALL_ADDRESS = $(shell cat /sys/module/the_usctm/parameters/sys_ni_syscall_address)
FREE_ENTRIES = $(shell cat /sys/module/the_usctm/parameters/free_entries)

all:
	gcc ./makefs.c -o ./makefs
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules 

all_valid_blocks:
	gcc ./makefs.c -o ./makefs -DALL_VALID
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
 
all_invalid_blocks:
	gcc ./makefs.c -o ./makefs -DALL_INVALID
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean
	
install_progetto_soa:
	insmod progetto_soa.ko sys_call_table_address=$(SYSCALL_TABLE_ADDRESS) sys_ni_syscall_address=$(SYS_NI_SYSCALL_ADDRESS) free_entries=$(FREE_ENTRIES)
	
remove_progetto_soa:
	rmmod progetto_soa
	
create-all:
	dd bs=4096 count=$(TOT_BLOCKS)  if=/dev/zero of=image
	./makefs image $(NBLOCKS)
	mkdir mount
	
mount-fs:
#echo $(MOUNT_DIR) > /sys/module/progetto_soa/parameters/mount_point
#mount -o loop -t userdatafs image $(MOUNT_DIR)
	mount -o loop -t userdatafs image ./mount

unmount-fs:
#umount $(MOUNT_DIR) ; rmdir $(MOUNT_DIR)
	umount ./mount ; rmdir ./mount

