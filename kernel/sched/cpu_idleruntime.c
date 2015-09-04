/*
   cpu_idleruntime.c: provide CPU usage data based on idle processing

   Copyright (C) 2012,2015 Carsten Emde <C.Emde@osadl.org>

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
#include <linux/cpu.h>

#include "sched.h"

DEFINE_PER_CPU(unsigned long long, idlestart);
DEFINE_PER_CPU(unsigned long long, idlestop);
DEFINE_PER_CPU(unsigned long long, idletime);
DEFINE_PER_CPU(unsigned long long, runtime);
DEFINE_PER_CPU(raw_spinlock_t, idleruntime_lock);

static DEFINE_PER_CPU(struct proc_dir_entry *, idleruntime_dir);
static struct proc_dir_entry *root_idleruntime_dir;

static void idleruntime_get(unsigned long cpu, unsigned long long *cpuidletime,
			    unsigned long long *cpuruntime)
{
	unsigned long long now;
	unsigned long flags;

	raw_spin_lock_irqsave(&per_cpu(idleruntime_lock, cpu), flags);

	/* Update runtime counter */
	now = cpu_clock(cpu);
	if (is_idle_task(cpu_rq(cpu)->curr))
		per_cpu(idletime, cpu) += now - per_cpu(idlestart, cpu);
	else
		per_cpu(runtime, cpu) += now - per_cpu(idlestop, cpu);

	*cpuidletime = per_cpu(idletime, cpu);
	*cpuruntime = per_cpu(runtime, cpu);

	raw_spin_unlock_irqrestore(&per_cpu(idleruntime_lock, cpu), flags);

}

static void idleruntime_output(struct seq_file *m, unsigned long long idletime,
		   unsigned long long runtime)
{
	seq_printf(m, "%llu %llu\n", idletime, runtime);
}

static int idleruntime_show(struct seq_file *m, void *v)
{
	unsigned long cpu = (unsigned long) m->private;
	unsigned long long cpuidletime, cpuruntime;

	idleruntime_get(cpu, &cpuidletime, &cpuruntime);
	idleruntime_output(m, cpuidletime, cpuruntime);

	return 0;
}

static int idleruntime_show_all(struct seq_file *m, void *v)
{
	unsigned long cpu;
	unsigned long long total_idletime = 0ULL, total_runtime = 0ULL;

	preempt_disable();

	for_each_present_cpu(cpu) {
		unsigned long long cpuidletime, cpuruntime;

		idleruntime_get(cpu, &cpuidletime, &cpuruntime);
		total_idletime += cpuidletime;
		total_runtime += cpuruntime;
	}

	preempt_enable();

	idleruntime_output(m, total_idletime, total_runtime);

	return 0;
}

static inline void idleruntime_reset1(unsigned long cpu)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&per_cpu(idleruntime_lock, cpu), flags);
	per_cpu(idletime, cpu) = per_cpu(runtime, cpu) = 0;
	per_cpu(idlestart, cpu) =  per_cpu(idlestop, cpu) = cpu_clock(cpu);
	raw_spin_unlock_irqrestore(&per_cpu(idleruntime_lock, cpu), flags);
}

static ssize_t idleruntime_reset(struct file *file, const char __user *buffer,
				 size_t len, loff_t *offset)
{
	unsigned long cpu = (unsigned long) PDE_DATA(file_inode(file));

	idleruntime_reset1(cpu);
	return len;
}

static ssize_t idleruntime_reset_all(struct file *file,
				    const char __user *buffer,
				    size_t len, loff_t *offset)
{
	unsigned long cpu;

	preempt_disable();

	for_each_present_cpu(cpu)
		idleruntime_reset1(cpu);

	preempt_enable();

	return len;
}

static int idleruntime_open_all(struct inode *inode, struct file *file)
{
	return single_open(file, idleruntime_show_all, PDE_DATA(inode));
}

static const struct file_operations idleruntime_all_fops = {
	.open = idleruntime_open_all,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = idleruntime_reset_all,
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

static void setup_procfiles(unsigned long cpu)
{
	char name[32];
	struct proc_dir_entry *idleruntime_cpudir = NULL;

	if (root_idleruntime_dir) {
		snprintf(name, sizeof(name), "cpu%lu", cpu);
		idleruntime_cpudir = proc_mkdir(name, root_idleruntime_dir);
	}

	if (idleruntime_cpudir) {
		proc_create_data("data", S_IRUGO, idleruntime_cpudir,
		    &idleruntime_fops, (void *) cpu);
		proc_create_data("reset", S_IWUGO, idleruntime_cpudir,
		    &idleruntime_fops, (void *) cpu);
	}
	per_cpu(idleruntime_dir, cpu) = idleruntime_cpudir;
}

static void unset_procfiles(unsigned long cpu)
{
	struct proc_dir_entry *idleruntime_cpudir =
	    per_cpu(idleruntime_dir, cpu);

	if (idleruntime_cpudir) {
		remove_proc_entry("reset", idleruntime_cpudir);
		remove_proc_entry("data", idleruntime_cpudir);
		proc_remove(idleruntime_cpudir);
		per_cpu(idleruntime_dir, cpu) = NULL;
	}
}

static int idleruntime_cpu_callback(struct notifier_block *nfb,
			       unsigned long action, void *hcpu)
{
	unsigned long cpu = (unsigned long) hcpu;

	switch (action) {
		case CPU_ONLINE:
			setup_procfiles(cpu);
			break;
#ifdef CONFIG_HOTPLUG_CPU
		case CPU_DEAD:
			unset_procfiles(cpu);
			break;
#endif
	}
	return NOTIFY_OK;
}

static struct notifier_block idleruntime_cpu_notifier =
{
	.notifier_call = idleruntime_cpu_callback,
};


static int __init idleruntime_init(void)
{
	root_idleruntime_dir = proc_mkdir("idleruntime", NULL);
	if (root_idleruntime_dir) {
		struct proc_dir_entry *idleruntime_alldir;
		unsigned long cpu, cpus = 0;

		for_each_possible_cpu(cpu) {
			per_cpu(idlestart, cpu) =  per_cpu(idlestop, cpu) =
			    cpu_clock(cpu);
			raw_spin_lock_init(&per_cpu(idleruntime_lock, cpu));
			cpus++;
		}

		setup_procfiles(0);

		if (cpus > 1) {
			idleruntime_alldir = proc_mkdir("all",
			    root_idleruntime_dir);
			proc_create_data("data", S_IRUGO, idleruntime_alldir,
			    &idleruntime_all_fops, NULL);
			proc_create_data("reset", S_IWUGO, idleruntime_alldir,
			    &idleruntime_all_fops, NULL);
		}

		register_cpu_notifier(&idleruntime_cpu_notifier);
	}
	return 0;
}

early_initcall(idleruntime_init);
