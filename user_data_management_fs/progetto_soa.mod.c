#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif

static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x32e21920, "module_layout" },
	{ 0xd9b85ef6, "lockref_get" },
	{ 0x3e43df8e, "init_user_ns" },
	{ 0x801b1225, "mount_bdev" },
	{ 0xda70dba8, "d_add" },
	{ 0xb3378a7b, "pv_ops" },
	{ 0xe2d5255a, "strcmp" },
	{ 0x6b10bee1, "_copy_to_user" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0xddb098e9, "__bread_gfp" },
	{ 0x9ec6ca96, "ktime_get_real_ts64" },
	{ 0x8db35eb9, "set_nlink" },
	{ 0x10aa427a, "__brelse" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0xd0da656b, "__stack_chk_fail" },
	{ 0x92997ed8, "_printk" },
	{ 0x3234379d, "unlock_new_inode" },
	{ 0x7ab3fff3, "kill_block_super" },
	{ 0x65487097, "__x86_indirect_thunk_rax" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0x7ef641cb, "register_filesystem" },
	{ 0x77377edd, "d_make_root" },
	{ 0x21ec6a82, "unregister_filesystem" },
	{ 0x4678fe15, "param_ops_ulong" },
	{ 0x88db9f48, "__check_object_size" },
	{ 0xf434e455, "iget_locked" },
	{ 0x58a39fb, "inode_init_owner" },
};

MODULE_INFO(depends, "");


MODULE_INFO(srcversion, "EEC09F969FE20F4D6952998");
