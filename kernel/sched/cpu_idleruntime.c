/*
   cpu_idleruntime.c: provide CPU usage data based on idle processing

   Copyright (C) 2012 Carsten Emde <C.Emde@osadl.org>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA.
*/

#include <linux/seq_file.h>
#include <linux/proc_fs.h>

#include "sched.h"

DEFINE_PER_CPU(unsigned long long, idlestart);
DEFINE_PER_CPU(unsigned long long, idlestop);
DEFINE_PER_CPU(unsigned long long, idletime);
DEFINE_PER_CPU(unsigned long long, runtime);
DEFINE_PER_CPU(raw_spinlock_t, idleruntime_lock);

static int idleruntime_show(struct seq_file *m, void *v)
{
	unsigned long cpu = (unsigned long) m->private;
	unsigned long long now;
	unsigned long flags;

	raw_spin_lock_irqsave(&per_cpu(idleruntime_lock, cpu), flags);

	/* Update runtime counter */
	now = cpu_clock(cpu);
	per_cpu(runtime, cpu) += now - per_cpu(idlestop, cpu);
	per_cpu(idlestop, cpu) = now;

	seq_printf(m, "%llu %llu\n", per_cpu(idletime, cpu),
	    per_cpu(runtime, cpu));

	raw_spin_unlock_irqrestore(&per_cpu(idleruntime_lock, cpu), flags);

	return 0;
}

static inline void idleruntime_reset1(int cpu)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&per_cpu(idleruntime_lock, cpu), flags);
	per_cpu(idletime, cpu) = per_cpu(runtime, cpu) = 0;
	per_cpu(idlestop, cpu) = cpu_clock(cpu);
	raw_spin_unlock_irqrestore(&per_cpu(idleruntime_lock, cpu), flags);
}

static ssize_t idleruntime_reset(struct file *file, const char __user *buffer,
				 size_t len, loff_t *offset)
{
	unsigned long cpu = (unsigned long) PDE_DATA(file_inode(file));

	idleruntime_reset1(cpu);
	return len;
}

static ssize_t idleruntime_resetall(struct file *file,
				    const char __user *buffer,
				    size_t len, loff_t *offset)
{
	unsigned long cpu;

	for_each_online_cpu(cpu)
		idleruntime_reset1(cpu);
	return len;
}

static const struct file_operations idleruntime_resetall_fops = {
	.write = idleruntime_resetall,
	.release = single_release,
};

static int idleruntime_open(struct inode *inode, struct file *file)
{
	return single_open(file, idleruntime_show, PDE_DATA(inode));
}

static const struct file_operations idleruntime_fops = {
	.open = idleruntime_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = idleruntime_reset,
	.release = single_release,
};

static int __init proc_idleruntime_init(void)
{
	unsigned long cpu;
	struct proc_dir_entry *root_idleruntime_dir;

	root_idleruntime_dir = proc_mkdir("idleruntime", NULL);
	if (!root_idleruntime_dir)
		return 0;

	if (!proc_create("resetall", S_IWUGO, root_idleruntime_dir,
	    &idleruntime_resetall_fops))
		return 0;

	for_each_possible_cpu(cpu) {
		char name[32];
		struct proc_dir_entry *idleruntime_cpudir;

		raw_spin_lock_init(&per_cpu(idleruntime_lock, cpu));

		snprintf(name, sizeof(name), "cpu%lu", cpu);
		idleruntime_cpudir = proc_mkdir(name, root_idleruntime_dir);
		if (!idleruntime_cpudir)
			return 0;

		if (!proc_create_data("data", S_IRUGO, idleruntime_cpudir,
		    &idleruntime_fops, (void *) cpu))
			return 0;

		if (!proc_create_data("reset", S_IWUGO, idleruntime_cpudir,
		    &idleruntime_fops, (void *) cpu)) {
			remove_proc_entry("data", idleruntime_cpudir);
			return 0;
		}
	}
	return 0;
}

module_init(proc_idleruntime_init);
