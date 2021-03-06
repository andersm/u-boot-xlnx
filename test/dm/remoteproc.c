// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2015
 * Texas Instruments Incorporated - http://www.ti.com/
 */
#include <common.h>
#include <dm.h>
#include <elf.h>
#include <errno.h>
#include <remoteproc.h>
#include <asm/io.h>
#include <dm/test.h>
#include <test/ut.h>
/**
 * dm_test_remoteproc_base() - test the operations after initializations
 * @uts:	unit test state
 *
 * Return:	0 if test passed, else error
 */
static int dm_test_remoteproc_base(struct unit_test_state *uts)
{
	if (!rproc_is_initialized())
		ut_assertok(rproc_init());

	/* Ensure we are initialized */
	ut_asserteq(true, rproc_is_initialized());


	/* platform data device 1 */
	ut_assertok(rproc_stop(0));
	ut_assertok(rproc_reset(0));
	/* -> invalid attempt tests */
	ut_asserteq(-EINVAL, rproc_start(0));
	ut_asserteq(-EINVAL, rproc_ping(0));
	/* Valid tests */
	ut_assertok(rproc_load(0, 1, 0));
	ut_assertok(rproc_start(0));
	ut_assertok(rproc_is_running(0));
	ut_assertok(rproc_ping(0));
	ut_assertok(rproc_reset(0));
	ut_assertok(rproc_stop(0));

	/* dt device device 1 */
	ut_assertok(rproc_stop(1));
	ut_assertok(rproc_reset(1));
	ut_assertok(rproc_load(1, 1, 0));
	ut_assertok(rproc_start(1));
	ut_assertok(rproc_is_running(1));
	ut_assertok(rproc_ping(1));
	ut_assertok(rproc_reset(1));
	ut_assertok(rproc_stop(1));

	/* dt device device 2 */
	ut_assertok(rproc_stop(0));
	ut_assertok(rproc_reset(0));
	/* -> invalid attempt tests */
	ut_asserteq(-EINVAL, rproc_start(0));
	ut_asserteq(-EINVAL, rproc_ping(0));
	/* Valid tests */
	ut_assertok(rproc_load(2, 1, 0));
	ut_assertok(rproc_start(2));
	ut_assertok(rproc_is_running(2));
	ut_assertok(rproc_ping(2));
	ut_assertok(rproc_reset(2));
	ut_assertok(rproc_stop(2));

	return 0;
}
DM_TEST(dm_test_remoteproc_base, DM_TESTF_SCAN_PDATA | DM_TESTF_SCAN_FDT);

#define DEVICE_TO_PHYSICAL_OFFSET	0x1000
/**
 * dm_test_remoteproc_elf() - test the ELF operations
 * @uts:	unit test state
 *
 * Return:	0 if test passed, else error
 */
static int dm_test_remoteproc_elf(struct unit_test_state *uts)
{
	u8 valid_elf32[] = {
		/* @0x00 - ELF HEADER - */
		/* ELF magic */
		0x7f, 0x45, 0x4c, 0x46,
		/* 32 Bits */
		0x01,
		/* Endianness */
#ifdef __LITTLE_ENDIAN
		0x01,
#else
		0x02,
#endif
		/* Version */
		0x01,
		/* Padding */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		/* Type : executable */
		0x02, 0x00,
		/* Machine: ARM */
		0x28, 0x00,
		/* Version */
		0x01, 0x00, 0x00, 0x00,
		/* Entry */
		0x00, 0x00, 0x00, 0x08,
		/* phoff (program header offset @ 0x40)*/
		0x40, 0x00, 0x00, 0x00,
		/* shoff (section header offset : none) */
		0x00, 0x00, 0x00, 0x00,
		/* flags */
		0x00, 0x00, 0x00, 0x00,
		/* ehsize (elf header size = 0x34) */
		0x34, 0x00,
		/* phentsize (program header size = 0x20) */
		0x20, 0x00,
		/* phnum (program header number : 1) */
		0x01, 0x00,
		/* shentsize (section heade size : none) */
		0x00, 0x00,
		/* shnum (section header number: none) */
		0x00, 0x00,
		/* shstrndx (section header name section index: none) */
		0x00, 0x00,
		/* padding */
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		/* @0x40 - PROGRAM HEADER TABLE - */
		/* type : PT_LOAD */
		0x01, 0x00, 0x00, 0x00,
		/* offset */
		0x00, 0x00, 0x00, 0x00,
		/* vaddr */
		0x00, 0x00, 0x00, 0x00,
		/* paddr : physical address */
		0x00, 0x00, 0x00, 0x00,
		/* filesz : 0x20 bytes (program header size) */
		0x20, 0x00, 0x00, 0x00,
		/* memsz = filesz */
		0x20, 0x00, 0x00, 0x00,
		/* flags : readable and exectuable */
		0x05, 0x00, 0x00, 0x00,
		/* padding */
		0x00, 0x00, 0x00, 0x00,
	};
	unsigned int size = ARRAY_SIZE(valid_elf32);
	struct udevice *dev;
	phys_addr_t loaded_firmware_paddr;
	void *loaded_firmware;
	u32 loaded_firmware_size;
	Elf32_Ehdr *ehdr = (Elf32_Ehdr *)valid_elf32;
	Elf32_Phdr *phdr = (Elf32_Phdr *)(valid_elf32 + ehdr->e_phoff);

	ut_assertok(uclass_get_device(UCLASS_REMOTEPROC, 0, &dev));

	/*
	 * In its Program Header Table, let the firmware specifies to be loaded
	 * at SDRAM_BASE *device* address (p_paddr field).
	 * Its size is defined by the p_filesz field.
	 */
	phdr->p_paddr = CONFIG_SYS_SDRAM_BASE;
	loaded_firmware_size = phdr->p_filesz;

	/*
	 * This *device* address is converted to a *physical* address by the
	 * device_to_virt() operation of sandbox_test_rproc which returns
	 * DeviceAddress + DEVICE_TO_PHYSICAL_OFFSET.
	 * This is where we expect to get the firmware loaded.
	 */
	loaded_firmware_paddr = phdr->p_paddr + DEVICE_TO_PHYSICAL_OFFSET;
	loaded_firmware = map_physmem(loaded_firmware_paddr,
				      loaded_firmware_size, MAP_NOCACHE);
	ut_assertnonnull(loaded_firmware);
	memset(loaded_firmware, 0, loaded_firmware_size);

	/* Load firmware in loaded_firmware, and verify it */
	ut_assertok(rproc_elf32_load_image(dev, (ulong)valid_elf32, size));
	ut_assertok(memcmp(loaded_firmware, valid_elf32, loaded_firmware_size));
	ut_asserteq(rproc_elf_get_boot_addr(dev, (unsigned long)valid_elf32),
		    0x08000000);
	unmap_physmem(loaded_firmware, MAP_NOCACHE);

	/* Invalid ELF Magic */
	valid_elf32[0] = 0;
	ut_asserteq(-EPROTONOSUPPORT,
		    rproc_elf32_sanity_check((ulong)valid_elf32, size));

	return 0;
}
DM_TEST(dm_test_remoteproc_elf, DM_TESTF_SCAN_PDATA | DM_TESTF_SCAN_FDT);
