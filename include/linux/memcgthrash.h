/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Per-memcg thrashing detector of the UA killer (Chen et al., Middleware '19),
 * ported from github.com/yncxcw/UAkiller (Linux 4.12.8).
 * Spec: notes/linux-oom/specs/ua-killer-port-spec.md.
 */
#ifndef __LINUX_MEMCGTHRASH_H
#define __LINUX_MEMCGTHRASH_H

#include <linux/spinlock_types.h>
#include <linux/types.h>

struct mem_cgroup;

/* vm.cgth_time: samples (one per second) of rising counters before a kill; 0 = off */
extern unsigned int default_curr_num;

#define MEM_CGROUP_MAX_THRASH_BUFFER 60

struct mem_cgroup_thrash {
	/* next slot of the circular buffers */
	int index;
	/* samples tested, vm.cgth_time when the memcg was created */
	int num;
	spinlock_t sr_lock;
	/* no sample before this time */
	unsigned long detec_jiffies;
	unsigned long pgmj_buffers[MEM_CGROUP_MAX_THRASH_BUFFER];
	unsigned long pgev_buffers[MEM_CGROUP_MAX_THRASH_BUFFER];
};

#ifdef CONFIG_MEMCG
void mem_cgroup_thrash_buffer_clear(struct mem_cgroup *memcg);
struct mem_cgroup_thrash *memcg_to_cg_thrash(struct mem_cgroup *memcg);
void mem_cgroup_thrash_init(struct mem_cgroup_thrash *cg_thrash);
bool mem_cgroup_thrash_add(struct mem_cgroup *memcg, unsigned long pgmj,
			   unsigned long pgev);
bool mem_cgroup_thrash_on(struct mem_cgroup *memcg);
unsigned long memcg_account_page_mjfault(struct mem_cgroup *memcg);
unsigned long memcg_account_page_eviction(struct mem_cgroup *memcg);
void mem_cgroup_by_oom_score(struct mem_cgroup *memcg);
#endif

#endif /* __LINUX_MEMCGTHRASH_H */
