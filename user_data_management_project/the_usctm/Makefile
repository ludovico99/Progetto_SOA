obj-m += the_usctm.o
the_usctm-objs += usctm.o ./lib/vtpmo.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean

install_the_usctm:
	insmod the_usctm.ko
	
remove_the_usctm:
	rmmod the_usctm