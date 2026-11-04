// SPDX-License-Identifier: GPL-2.0
/*
 * Per-memcg thrashing detector of the UA killer (Chen et al., Middleware '19),
 * ported from github.com/yncxcw/UAkiller (Linux 4.12.8) as is, quirks included.
 * Spec: notes/linux-oom/specs/ua-killer-port-spec.md.
 */
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/memcgthrash.h>
#include <linux/memcontrol.h>
#include <linux/spinlock.h>
#include <linux/sysctl.h>

/* default thrashing tolerance (samples), vm.cgth_time */
unsigned int default_curr_num = 3;

/* called with sr_lock held, or before the memcg is visible */
static void mem_cgthrash_thrash_buffer_clear(struct mem_cgroup_thrash *cg_thrash)
{
	int i;

	for (i = 0; i < MEM_CGROUP_MAX_THRASH_BUFFER; i++) {
		cg_thrash->pgmj_buffers[i] = 0;
		cg_thrash->pgev_buffers[i] = 0;
	}
	cg_thrash->index = 0;
}

void mem_cgroup_thrash_buffer_clear(struct mem_cgroup *memcg)
{
	struct mem_cgroup_thrash *cg_thrash = memcg_to_cg_thrash(memcg);

	spin_lock(&cg_thrash->sr_lock);
	mem_cgthrash_thrash_buffer_clear(cg_thrash);
	spin_unlock(&cg_thrash->sr_lock);
}

/* called from mem_cgroup_alloc() */
void mem_cgroup_thrash_init(struct mem_cgroup_thrash *cg_thrash)
{
	cg_thrash->num = default_curr_num;
	cg_thrash->index = 0;
	cg_thrash->detec_jiffies = jiffies;
	mem_cgthrash_thrash_buffer_clear(cg_thrash);
	spin_lock_init(&cg_thrash->sr_lock);
}

/*
 * Add one sample of the major faults (pgmj) and evictions + activations (pgev)
 * counters, at most one per second. Returns true if a sample was added.
 */
bool mem_cgroup_thrash_add(struct mem_cgroup *memcg, unsigned long pgmj,
			   unsigned long pgev)
{
	struct mem_cgroup_thrash *cg_thrash = memcg_to_cg_thrash(memcg);

	spin_lock(&cg_thrash->sr_lock);
	if (time_before(jiffies, cg_thrash->detec_jiffies)) {
		spin_unlock(&cg_thrash->sr_lock);
		return false;
	}

	/* no sample for the length of the buffer: start over */
	if (!time_before(jiffies, cg_thrash->detec_jiffies +
			 MEM_CGROUP_MAX_THRASH_BUFFER * HZ))
		mem_cgthrash_thrash_buffer_clear(cg_thrash);

	if (cg_thrash->index == MEM_CGROUP_MAX_THRASH_BUFFER)
		cg_thrash->index = 0;
	cg_thrash->pgmj_buffers[cg_thrash->index] = pgmj;
	cg_thrash->pgev_buffers[cg_thrash->index] = pgev;
	cg_thrash->index++;
	cg_thrash->detec_jiffies = jiffies + HZ;
	spin_unlock(&cg_thrash->sr_lock);

	return true;
}

/*
 * True if each of the last @num samples, walking back from @index, is larger
 * than the one before it. Kept as in the original, including its sentinel
 * (last == -1) and its wrap (index 0 is replaced by the last slot before it is
 * read, and the walk can read before the array when @index starts at 0).
 */
static bool buffers_increase(int num, int index, unsigned long *buffers)
{
	bool thrash = true;
	unsigned long last = -1;

	for (; num > 0; num--, index--) {
		if (last == -1) {
			last = buffers[index];
			continue;
		}

		if (index == 0)
			index = MEM_CGROUP_MAX_THRASH_BUFFER - 1;

		if (last <= buffers[index]) {
			thrash = false;
			break;
		}

		last = buffers[index];
	}

	return thrash;
}

/* true if either counter rose over the last num samples; read without the lock */
bool mem_cgroup_thrash_on(struct mem_cgroup *memcg)
{
	struct mem_cgroup_thrash *cg_thrash = memcg_to_cg_thrash(memcg);
	/* index points to the next empty slot */
	int index = cg_thrash->index - 1;
	int num = cg_thrash->num;
	bool thrash_pgmj = buffers_increase(num, index, cg_thrash->pgmj_buffers);
	bool thrash_pgev = buffers_increase(num, index, cg_thrash->pgev_buffers);

	return thrash_pgmj || thrash_pgev;
}

static const struct ctl_table memcg_thrash_sysctls[] = {
	{
		.procname	= "cgth_time",
		.data		= &default_curr_num,
		.maxlen		= sizeof(default_curr_num),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE_HUNDRED,
	},
};

static int __init memcg_thrash_sysctl_init(void)
{
	register_sysctl_init("vm", memcg_thrash_sysctls);
	return 0;
}
subsys_initcall(memcg_thrash_sysctl_init);
