
obj-m += progetto_soa.o 
progetto_soa-objs += lib/scth.o userdatamgmt.o file_system/file.o file_system/dir.o rcu_utils.o
NBLOCKS = 10
TOT_BLOCKS = $(shell expr $(NBLOCKS) + 2)
MOUNT_DIR = /home/ludovico99/Scrivania/Progetto_SOA/user_data_management_project/mount/

A = $(shell cat /sys/module/the_usctm/parameters/sys_call_table_address)

all:
	gcc ./file_system/makefs.c -o ./file_system/makefs
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

	
install_progetto_soa:
	insmod progetto_soa.ko the_syscall_table=$(A)

remove_progetto_soa:
	rmmod progetto_soa
	
create-all:
#	mknod $(dev) c 236 0

	dd bs=4096 count=$(TOT_BLOCKS)  if=/dev/zero of=image
	./file_system/makefs image
	mkdir mount
	
mount-fs:
#	mkfs -t userdatafs $(dev)
	echo $(MOUNT_DIR) > /sys/module/progetto_soa/parameters/mount_point
	mount -o loop -t userdatafs image $(MOUNT_DIR)

unmount-fs:
#	rm $(dev)
	umount ./mount ; rmdir ./mount/

