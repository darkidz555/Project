/*
 * Copyright (C) 2013 TripNDroid Mobile Engineering
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <linux/export.h>
#include <linux/percpu.h>
#include <linux/hrtimer.h>
#include <linux/sched.h>
#include <linux/math64.h>

static DEFINE_PER_CPU(u64, nr_prod_sum);
static DEFINE_PER_CPU(u64, last_time);
static DEFINE_PER_CPU(u64, nr);
static DEFINE_PER_CPU(spinlock_t, nr_lock) = __SPIN_LOCK_UNLOCKED(nr_lock);
static s64 last_get_time;

/**
 * sched_running_avg
 * @return: Average nr_running and iowait value since last poll.
 *	    Returns the avg * 100 to return up to two decimal points
 *	    of accuracy.
 *
 * Obtains the average nr_running value since the last poll.
 * This function may not be called concurrently with itself
 */
void sched_running_avg(int *avg)
{
	int cpu;
	u64 curr_time = sched_clock();
	u64 diff = curr_time - last_get_time;
	u64 tmp_avg = 0;

	*avg = 0;

	if (!diff)
		return;

	last_get_time = curr_time;
	/* read and reset nr_running counts */
	for_each_possible_cpu(cpu) {
		unsigned long flags;

		spin_lock_irqsave(&per_cpu(nr_lock, cpu), flags);
		tmp_avg += per_cpu(nr_prod_sum, cpu);
		tmp_avg += per_cpu(nr, cpu) *
			(curr_time - per_cpu(last_time, cpu));
		per_cpu(last_time, cpu) = curr_time;
		per_cpu(nr_prod_sum, cpu) = 0;
		spin_unlock_irqrestore(&per_cpu(nr_lock, cpu), flags);
	}

	*avg = (int)div64_u64(tmp_avg * 10, diff);
}
EXPORT_SYMBOL(sched_running_avg);

/**
 * sched_update_tdf
 * @cpu: The core id of the nr running driver.
 * @nr: Updated nr running value for cpu.
 * @inc: Whether we are increasing or decreasing the count
 * @return: N/A
 *
 * Update average with latest nr_running value for CPU
 */
void sched_update_tdf(int cpu, unsigned long nr_running, bool inc)
{
	int diff;
	s64 curr_time;
	unsigned long flags;

	spin_lock_irqsave(&per_cpu(nr_lock, cpu), flags);
	curr_time = sched_clock();
	diff = curr_time - per_cpu(last_time, cpu);
	per_cpu(last_time, cpu) = curr_time;
	per_cpu(nr, cpu) = nr_running + (inc ? 1 : -1);

	per_cpu(nr_prod_sum, cpu) += nr_running * diff;
	spin_unlock_irqrestore(&per_cpu(nr_lock, cpu), flags);
}
EXPORT_SYMBOL(sched_update_tdf);
