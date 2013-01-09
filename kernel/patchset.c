/*
 * kernel/patchset.c
 * Save the current patchset in the kernel
 *
 * Copyright (C) 2013 Carsten Emde <C.Emde@osadl.org>
 *
 * Based on kernel/configs.c. Credits go to
 * - Khalid Aziz <khalid_aziz@hp.com>
 * - Randy Dunlap <rdunlap@xenotime.net>
 * - Al Stone <ahs3@fc.hp.com>
 * - Hewlett-Packard Company
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/uaccess.h>

/**************************************************/
/* the patchset used to create the current kernel */

/*
 * Define kernel_patchset_data and kernel_patchset_data_size. It contains the
 * wrapped and compressed patchset.  The patchset is first compressed with
 * gzip and then bounded by two eight byte magic numbers to allow extraction
 * from a binary kernel image:
 *
 *   IKPAQ_ST
 *   <image>
 *   IKPAQ_ED
 */
#define MAGIC_START	"IKPAQ_ST"
#define MAGIC_END	"IKPAQ_ED"
#include "patchset_data.h"


#define MAGIC_SIZE (sizeof(MAGIC_START) - 1)
#define kernel_patchset_data_size \
	(sizeof(kernel_patchset_data) - 1 - MAGIC_SIZE * 2)

#ifdef CONFIG_IKPATCHSET_PROC

static ssize_t
ikpatchset_read_current(struct file *file, char __user *buf,
		      size_t len, loff_t *offset)
{
	return simple_read_from_buffer(buf, len, offset,
				       kernel_patchset_data + MAGIC_SIZE,
				       kernel_patchset_data_size);
}

static const struct file_operations ikpatchset_file_ops = {
	.owner = THIS_MODULE,
	.read = ikpatchset_read_current,
	.llseek = default_llseek,
};

static int __init ikpatchset_init(void)
{
	struct proc_dir_entry *entry;

	/* create the patchset file entry */
	entry = proc_create("patchset.tar.gz", S_IFREG | S_IRUGO, NULL,
			    &ikpatchset_file_ops);
	if (!entry)
		return -ENOMEM;

	proc_set_size(entry, kernel_patchset_data_size);

	return 0;
}

static void __exit ikpatchset_cleanup(void)
{
	remove_proc_entry("patchset.tar.gz", NULL);
}

module_init(ikpatchset_init);
module_exit(ikpatchset_cleanup);

#endif /* CONFIG_IKPATCHSET_PROC */

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Carsten Emde");
MODULE_DESCRIPTION("Save the current patchset in the kernel");
