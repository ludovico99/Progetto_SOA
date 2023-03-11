the_usctm_dir = $(shell pwd)/Linux-sys_call_table-discoverer/
progetto_soa_dir = $(shell pwd)/user_data_management_fs/

# all:
# 	make -C $(the_usctm_dir) all
# 	make -C $(progetto_soa_dir) all

build_progetto_soa:
	make -C $(progetto_soa_dir) all 

build_the_usctm:
	make -C $(the_usctm_dir) all

# clean:
# 	make -C $(the_usctm_dir) clean
# 	make -C $(progetto_soa_dir) clean

clean_the_usctm:
	make -C $(the_usctm_dir) clean

clean_progetto_soa:
	make -C $(progetto_soa_dir) clean

# install:
# 	make -C $(the_usctm_dir) install_the_usctm
# 	make -C $(progetto_soa_dir) install_progetto_soa

install_the_usctm:
	make -C $(the_usctm_dir) install_the_usctm

install_progetto_soa:
	make -C $(progetto_soa_dir) install_progetto_soa
	make -C $(progetto_soa_dir) create-fs
	make -C $(progetto_soa_dir) mount-fs

# remove:
# 	make -C $(the_usctm_dir) remove_the_usctm
# 	make -C $(progetto_soa_dir) remove_progetto_soa

remove_the_usctm:
	make -C $(the_usctm_dir) remove_the_usctm

remove_progetto_soa:
	make -C $(progetto_soa_dir) unmount-fs
	make -C $(progetto_soa_dir) remove_progetto_soa






