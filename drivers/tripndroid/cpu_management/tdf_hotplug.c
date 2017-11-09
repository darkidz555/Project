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

#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/export.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/notifier.h>
#include <linux/td_framework.h>
#include <linux/tdf_hotplug.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/rq_stats.h>
#include <trace/events/power.h>

struct tdf_hotplug_cpudata_t {
	int online;
	u64 previous_integral;
	unsigned int avg;
	bool integral_sampled;
	u64 prev_timestamp;
	struct mutex hp_cpu_mutex;
};
static DEFINE_PER_CPU(struct tdf_hotplug_cpudata_t, tdf_hotplug_cpudata);

static DEFINE_MUTEX(tdf_hotplug_cpu_lock);
static DEFINE_MUTEX(tdf_hotplug_suspend_lock);

struct delayed_work tdf_hotplug_w;

static struct tdf_hotplug {
	unsigned int sample_ms;
	unsigned int pause;
	unsigned int delay;
	unsigned int idle_freq;
        unsigned int max_cpus;
        unsigned int min_cpus;
    struct notifier_block notif;
} config = {
	.sample_ms = TDF_HOTPLUG_SAMPLE_MS,
	.pause = TDF_HOTPLUG_PAUSE,
	.delay = TDF_HOTPLUG_DELAY,
	.idle_freq = TDF_FREQ_IDLE,
        .max_cpus = CONFIG_NR_CPUS,
        .min_cpus = 1,
};
struct notifier_block __refdata tdf_hotplug_fb_notif;

unsigned int state = TDF_HOTPLUG_IDLE;
unsigned int slowest_cpu_rate;

extern unsigned int tdf_powersave_active;
extern unsigned int tdf_suspend_state;
extern unsigned int nr_run_hysteresis;

extern unsigned long avg_nr_running(void);

static bool was_paused = false;
static bool run_timer = false;

static cputime64_t tdf_pause_timer = 0;

static s64 current_time = 0;

static unsigned int normal_thresholds[] = {
	3,  5,  7,  9,  11,  13,  15,  UINT_MAX
};

static unsigned int powersaving_thresholds[] = {
    5,  UINT_MAX
};

static unsigned int NwNs_Threshold[16] = {20,  12,  74,  10,  82,  20,  68,  14,  86,  18,  62,  18,  78,  20,  52,  20};
static unsigned int TwTs_Threshold[16] = {140, 100, 120, 100, 100, 100, 140, 100, 100, 140, 100, 140, 120, 120, 120, 110};

static unsigned long cpu_getspeed(unsigned int cpu)
{
	unsigned int cur_freq = cpufreq_quick_get(cpu);
    return cur_freq;
}

static int get_slowest_cpu(void)
{
    int i, cpu = 0;
    unsigned long rate, slow_rate = 0;

    for (i = 1; i < config.max_cpus; i++) {
		if (!cpu_online(i))
			continue;
		rate = cpu_getspeed(i);
		if (slow_rate == 0) {
			cpu = i;
			slow_rate = rate;
			continue;
		}
		if ((rate <= slow_rate) && (slow_rate != 0)) {
			cpu = i;
			slow_rate = rate;
		}
    }
    return cpu;
}

static void tdf_cpu_up(int cpu) {
    if (!cpu_online(cpu)) {
		mutex_lock(&per_cpu(tdf_hotplug_cpudata, cpu).hp_cpu_mutex);
		cpu_up(cpu);
		per_cpu(tdf_hotplug_cpudata, cpu).online = true;
		pr_info("[TDF] we just placed cpu %u online\n", cpu);
		mutex_unlock(&per_cpu(tdf_hotplug_cpudata, cpu).hp_cpu_mutex);
    }
}
EXPORT_SYMBOL_GPL(tdf_cpu_up);

static void tdf_cpu_down(int cpu) {
    if (cpu_online(cpu)) {
		mutex_lock(&per_cpu(tdf_hotplug_cpudata, cpu).hp_cpu_mutex);
		cpu_down(cpu);
		per_cpu(tdf_hotplug_cpudata, cpu).online = false;
		pr_info("[TDF] we just placed cpu %u offline\n", cpu);
		mutex_unlock(&per_cpu(tdf_hotplug_cpudata, cpu).hp_cpu_mutex);
    }
}
EXPORT_SYMBOL_GPL(tdf_cpu_down);

static unsigned int calculate_load(void)
{
	unsigned int avg_nr_run = avg_nr_running();
	unsigned int nr_run, nr_fshift, select_threshold;
	unsigned int nr_run_last, nr_threshold;

	if (!run_timer) {
		run_timer = true;
		current_time = ktime_to_ms(ktime_get());
	}

	if (tdf_powersave_active == 1 || tdf_suspend_state == 1) {
		nr_fshift = 2;
		select_threshold = ARRAY_SIZE(powersaving_thresholds);
	}
	else {
		nr_fshift = num_possible_cpus() - 1;
		select_threshold = ARRAY_SIZE(normal_thresholds);
	}

	for (nr_run = 1; nr_run < select_threshold; nr_run++) {
		if (tdf_powersave_active == 1 || tdf_suspend_state == 1) {
			nr_threshold = powersaving_thresholds[nr_run - 1];
        }
		else {
			nr_threshold = normal_thresholds[nr_run - 1];
        }

		if (nr_run_last <= nr_run)
			nr_threshold += (1 << nr_fshift) / nr_run_hysteresis;
		if (avg_nr_run <= (nr_threshold << (FSHIFT - nr_fshift)))
			break;
	}
	nr_run_last = nr_run;

	return nr_run;
}

static int mp_decision(void)
{
	int next_state = TDF_HOTPLUG_IDLE;
	int index, online_cpus, req_cpus;
	int slowest_cpu, current_run;

	static cputime64_t total_time = 0;
	cputime64_t this_time = 0;

	if (state == TDF_HOTPLUG_DISABLED)
		return TDF_HOTPLUG_DISABLED;

	this_time = ktime_to_ms(ktime_get()) - current_time;
	total_time += this_time;

	sched_running_avg(&current_run);

	online_cpus = num_online_cpus();
	req_cpus = calculate_load();

#ifdef CONFIG_TDF_DEBUG
	pr_info("[TDF] total_time = %lld\n", total_time);
	pr_info("[TDF] current_run = %u\n", current_run);
	pr_info("[TDF] req_cpus = %u, online_cpus = %u\n", req_cpus, online_cpus);
#endif

	slowest_cpu = get_slowest_cpu();
	slowest_cpu_rate = cpu_getspeed(slowest_cpu);

	if (online_cpus) {
		index = (online_cpus - 1) * 2;
		if ((online_cpus < config.max_cpus) && (current_run >= NwNs_Threshold[index])) {
			if ((total_time > TwTs_Threshold[index]) && (online_cpus < req_cpus)) {
				next_state = TDF_HOTPLUG_UP;
			} else {
				if ((slowest_cpu_rate <= config.idle_freq) && !(online_cpus < req_cpus)) {
					next_state = TDF_HOTPLUG_IDLE;
					total_time = 0;
				}
			}
		} else if ((online_cpus > config.min_cpus) && (current_run <= NwNs_Threshold[index+1])) {
			if ((total_time > TwTs_Threshold[index+1]) && (online_cpus > req_cpus)) {
				next_state = TDF_HOTPLUG_DOWN;
			} else {
				if ((slowest_cpu_rate > config.idle_freq) && !(online_cpus > req_cpus)) {
					next_state = TDF_HOTPLUG_IDLE;
					total_time = 0;
				}
			}
		} else {
			next_state = TDF_HOTPLUG_IDLE;
			total_time = 0;
		}
	} else {
		total_time = 0;
	}

	if (next_state != TDF_HOTPLUG_IDLE) {
		total_time = 0;
	}

	run_timer = false;

	return next_state;
}

static void tdf_hotplug_wt(struct work_struct *work)
{
	unsigned int cpu = nr_cpu_ids;

	if (tdf_pause_timer >= ktime_to_ms(ktime_get()))
		goto out;

	if (tdf_suspend_state == 1)
		goto out;

	if (!mutex_trylock(&tdf_hotplug_cpu_lock))
		goto out;

	if (was_paused) {
		for_each_possible_cpu(cpu) {
			if (cpu_online(cpu))
				per_cpu(tdf_hotplug_cpudata, cpu).online = true;
			else if (!cpu_online(cpu))
				per_cpu(tdf_hotplug_cpudata, cpu).online = false;
		}
		was_paused = false;
	}

	state = mp_decision();
	switch (state) {
	case TDF_HOTPLUG_DISABLED:
	case TDF_HOTPLUG_IDLE:
		break;
	case TDF_HOTPLUG_DOWN:
			cpu = get_slowest_cpu();
			if (cpu < nr_cpu_ids) {
				if ((per_cpu(tdf_hotplug_cpudata, cpu).online == true) && (cpu_online(cpu))) {
					tdf_cpu_down(cpu);
				}
			} else {
				if (per_cpu(tdf_hotplug_cpudata, cpu).online != cpu_online(cpu)) {
					tdf_pause_timer = ktime_to_ms(ktime_get()) + config.pause;
					was_paused = true;
				}
			}
		break;
	case TDF_HOTPLUG_UP:
			cpu = cpumask_next_zero(0, cpu_online_mask);
			if (cpu < nr_cpu_ids) {
				if ((per_cpu(tdf_hotplug_cpudata, cpu).online == false) && (!cpu_online(cpu))) {
					tdf_cpu_up(cpu);
				}
			} else {
				if (per_cpu(tdf_hotplug_cpudata, cpu).online != cpu_online(cpu)) {
					tdf_pause_timer = ktime_to_ms(ktime_get()) + config.pause;
					was_paused = true;
				}
			}
		break;
	default:
		pr_info("[TDF] oops! hit an invalid state %d\n", state);
	}
	mutex_unlock(&tdf_hotplug_cpu_lock);

out:
	if (state != TDF_HOTPLUG_DISABLED) {
		schedule_delayed_work_on(0, &tdf_hotplug_w, msecs_to_jiffies(config.delay));
    }
	return;
}

static void tdf_hotplug_suspend(void)
{
	int cpu = nr_cpu_ids;
	int suspend_cores = ARRAY_SIZE(powersaving_thresholds);

	pr_info("[TDF] tdf_hotplug_suspend = starting\n");

	tdf_suspend_state = 1;

	mutex_lock(&tdf_hotplug_suspend_lock);
	cancel_delayed_work_sync(&tdf_hotplug_w);
	mutex_unlock(&tdf_hotplug_suspend_lock);

	for (cpu = suspend_cores; cpu < config.max_cpus; cpu++) {
		if (cpu_online(cpu))
			tdf_cpu_down(cpu);
	}
	return;
}

static void tdf_hotplug_resume(void)
{
	int cpu = nr_cpu_ids;

	if (!tdf_suspend_state)
		return;

	pr_info("[TDF] tdf_hotplug_resume = starting\n");

	was_paused = true;
	tdf_suspend_state = 0;

	mutex_lock(&tdf_hotplug_suspend_lock);
	schedule_delayed_work_on(0, &tdf_hotplug_w, msecs_to_jiffies(0));
	mutex_unlock(&tdf_hotplug_suspend_lock);

	for (cpu = config.min_cpus; cpu < config.max_cpus; cpu++) {
		if (!cpu_online(cpu))
			tdf_cpu_up(cpu);
	}
	return;
}

static int tdf_hotplug_fb_notifier_callback(struct notifier_block *self,
                unsigned long event, void *data)
{
    struct fb_event *evdata = data;
    int *blank;

    if (event == FB_EVENT_BLANK) {
        blank = evdata->data;

        switch (*blank) {
        case FB_BLANK_UNBLANK:
            tdf_hotplug_resume();
            break;
        case FB_BLANK_POWERDOWN:
            tdf_hotplug_suspend();
            break;
        }
    }
    return 0;
}

struct notifier_block tdf_hotplug_fb_notif = {
    .notifier_call = tdf_hotplug_fb_notifier_callback,
};

static int __init tdf_hotplug_init(void)
{
	int cpu, ret = 0;
	int sample_ms = usecs_to_jiffies(TDF_HOTPLUG_SAMPLE_MS);

	ret = fb_register_client(&tdf_hotplug_fb_notif);
	if (ret) {
		pr_info("[TDF] FB register failed\n");
		return ret;
	}

	if (num_online_cpus() > 1)
		sample_ms -= jiffies % sample_ms;

	for_each_possible_cpu(cpu) {
        mutex_init(&(per_cpu(tdf_hotplug_cpudata, cpu).hp_cpu_mutex));
		per_cpu(tdf_hotplug_cpudata, cpu).online = true;
	}

	was_paused = true;

	if (state != TDF_HOTPLUG_DISABLED)
		INIT_DELAYED_WORK(&tdf_hotplug_w, tdf_hotplug_wt);
		schedule_delayed_work_on(0, &tdf_hotplug_w, msecs_to_jiffies(sample_ms));

	return ret;
}
late_initcall(tdf_hotplug_init);
