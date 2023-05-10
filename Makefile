the_usctm_dir = $(shell pwd)/the_usctm/
progetto_soa_dir = $(shell pwd)/user_data_management_project/


build_progetto_soa:
	make -C $(progetto_soa_dir) all 

build_the_usctm:
	make -C $(the_usctm_dir) all

clean_the_usctm:
	make -C $(the_usctm_dir) clean

clean_progetto_soa:
	make -C $(progetto_soa_dir) clean

install_the_usctm:
	make -C $(the_usctm_dir) install_the_usctm

install_progetto_soa:
	make -C $(progetto_soa_dir) install_progetto_soa
	make -C $(progetto_soa_dir) create-all
	make -C $(progetto_soa_dir) mount-fs

remove_the_usctm:
	make -C $(the_usctm_dir) remove_the_usctm

remove_progetto_soa:
	make -C $(progetto_soa_dir) unmount-fs
	make -C $(progetto_soa_dir) remove_progetto_soa






