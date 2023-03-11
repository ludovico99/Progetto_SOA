
obj-m += progetto_soa.o 
progetto_soa-objs += user_data_management_fs.o file.o dir.o lib/scth.o

NBLOCKS = 20

A = $(shell cat /sys/module/the_usctm/parameters/sys_call_table_address)

all:
	gcc format_fs.c -o format_fs
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

	
install_progetto_soa:
	insmod progetto_soa.ko the_syscall_table=$(A)

remove_progetto_soa:
	rmmod progetto_soa

create-fs: 
	dd bs=4096 count=$(NBLOCKS) if=/dev/zero of=image
	./format_fs image
	mkdir ./mount
	
mount-fs:
	mount -o loop -t userdatafs image ./mount/

unmount-fs:
	umount ./mount/ ; rmdir ./mount/
