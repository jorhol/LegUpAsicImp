#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

__visible struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0xb89a34a1, __VMLINUX_SYMBOL_STR(module_layout) },
	{ 0x6bc3fbc0, __VMLINUX_SYMBOL_STR(__unregister_chrdev) },
	{ 0x16e17547, __VMLINUX_SYMBOL_STR(kmalloc_caches) },
	{ 0xd2b09ce5, __VMLINUX_SYMBOL_STR(__kmalloc) },
	{ 0xc897c382, __VMLINUX_SYMBOL_STR(sg_init_table) },
	{ 0x8d108923, __VMLINUX_SYMBOL_STR(up_read) },
	{ 0xc8b57c27, __VMLINUX_SYMBOL_STR(autoremove_wake_function) },
	{ 0x769dc43f, __VMLINUX_SYMBOL_STR(dma_set_mask) },
	{ 0xb4549cdc, __VMLINUX_SYMBOL_STR(pci_disable_device) },
	{ 0xdb4a0814, __VMLINUX_SYMBOL_STR(pcie_capability_read_dword) },
	{ 0xea40912b, __VMLINUX_SYMBOL_STR(device_destroy) },
	{ 0x897ee3d1, __VMLINUX_SYMBOL_STR(__register_chrdev) },
	{ 0x3fec048f, __VMLINUX_SYMBOL_STR(sg_next) },
	{ 0xa1a0a7ef, __VMLINUX_SYMBOL_STR(x86_dma_fallback_dev) },
	{ 0x7082ffee, __VMLINUX_SYMBOL_STR(pci_release_regions) },
	{ 0x73a11961, __VMLINUX_SYMBOL_STR(down_read) },
	{ 0xf432dd3d, __VMLINUX_SYMBOL_STR(__init_waitqueue_head) },
	{ 0x4f8b5ddb, __VMLINUX_SYMBOL_STR(_copy_to_user) },
	{ 0xf8e67854, __VMLINUX_SYMBOL_STR(pci_set_master) },
	{ 0x5d165205, __VMLINUX_SYMBOL_STR(pci_restore_state) },
	{ 0xdc2bead1, __VMLINUX_SYMBOL_STR(current_task) },
	{ 0x27e1a049, __VMLINUX_SYMBOL_STR(printk) },
	{ 0x6c227c38, __VMLINUX_SYMBOL_STR(device_create) },
	{ 0x2072ee9b, __VMLINUX_SYMBOL_STR(request_threaded_irq) },
	{ 0x42c8de35, __VMLINUX_SYMBOL_STR(ioremap_nocache) },
	{ 0x669024b6, __VMLINUX_SYMBOL_STR(get_user_pages) },
	{ 0xd62c833f, __VMLINUX_SYMBOL_STR(schedule_timeout) },
	{ 0xbdfb6dbb, __VMLINUX_SYMBOL_STR(__fentry__) },
	{ 0xd2cfd982, __VMLINUX_SYMBOL_STR(pci_enable_msi_range) },
	{ 0x8b190a16, __VMLINUX_SYMBOL_STR(pci_unregister_driver) },
	{ 0x3b88f3a0, __VMLINUX_SYMBOL_STR(kmem_cache_alloc_trace) },
	{ 0xcf21d241, __VMLINUX_SYMBOL_STR(__wake_up) },
	{ 0x37a0cba, __VMLINUX_SYMBOL_STR(kfree) },
	{ 0xd8a22f2e, __VMLINUX_SYMBOL_STR(pci_request_regions) },
	{ 0x5c8b5ce8, __VMLINUX_SYMBOL_STR(prepare_to_wait) },
	{ 0xb3931e6c, __VMLINUX_SYMBOL_STR(pci_disable_msi) },
	{ 0x90206ede, __VMLINUX_SYMBOL_STR(dma_supported) },
	{ 0xedc03953, __VMLINUX_SYMBOL_STR(iounmap) },
	{ 0x9af4f94d, __VMLINUX_SYMBOL_STR(__pci_register_driver) },
	{ 0x3a04aa8e, __VMLINUX_SYMBOL_STR(put_page) },
	{ 0x3d1792ec, __VMLINUX_SYMBOL_STR(class_destroy) },
	{ 0xfa66f77c, __VMLINUX_SYMBOL_STR(finish_wait) },
	{ 0x7235edf, __VMLINUX_SYMBOL_STR(pcie_capability_write_dword) },
	{ 0x28318305, __VMLINUX_SYMBOL_STR(snprintf) },
	{ 0xde05cf1d, __VMLINUX_SYMBOL_STR(pci_enable_device) },
	{ 0x4f6b400b, __VMLINUX_SYMBOL_STR(_copy_from_user) },
	{ 0xf431e371, __VMLINUX_SYMBOL_STR(__class_create) },
	{ 0xa1ddb670, __VMLINUX_SYMBOL_STR(dma_ops) },
	{ 0xf20dabd8, __VMLINUX_SYMBOL_STR(free_irq) },
	{ 0x233cab45, __VMLINUX_SYMBOL_STR(pci_save_state) },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";

MODULE_ALIAS("pci:v000010EEd*sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v00001172d*sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "B295ACE4EAD21AD0837E0D9");
