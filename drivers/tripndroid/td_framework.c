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

#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/td_framework.h>

/* not for userspace */
unsigned int nr_run_hysteresis = 4;
unsigned int tdf_suspend_state = 0;
unsigned int tdf_cpu_load = 0;

/* make available to userspace */
unsigned int tdf_powersave_active = 0;
unsigned int tdf_fast_charge = 0;

/* create sysfs structure start */
struct kobject *tdf_kobject;

#define show_one(file_name, value)					\
static ssize_t show_##file_name						\
(struct kobject *kobj, struct attribute *attr, char *buf)               \
{									\
	return sprintf(buf, "%u\n", value);				\
}
show_one(powersave_active, tdf_powersave_active);
show_one(fast_charge, tdf_fast_charge);
#ifdef TDF_SUSPEND_DEBUG
show_one(suspend_state, tdf_suspend_state);
#endif

static ssize_t store_powersave_active(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int value;
	int ret;
	ret = sscanf(buf, "%u", &value);
	if (ret != 1)
		return -EINVAL;

	tdf_powersave_active = value;

	return count;
}
define_one_global_rw(powersave_active);

static ssize_t store_fast_charge(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int value;
	int ret;
	ret = sscanf(buf, "%u", &value);
	if (ret != 1)
		return -EINVAL;

	tdf_fast_charge = value;

	return count;
}
define_one_global_rw(fast_charge);

#ifdef TDF_SUSPEND_DEBUG
static ssize_t store_suspend_state(struct kobject *a, struct attribute *b,
				   const char *buf, size_t count)
{
	unsigned int value;
	int ret;
	ret = sscanf(buf, "%u", &value);
	if (ret != 1)
		return -EINVAL;

	tdf_suspend_state = value;

	return count;
}
define_one_global_rw(suspend_state);
#endif

static struct attribute *tdf_attributes[] = {
	&powersave_active.attr,
	&fast_charge.attr,
#ifdef TDF_SUSPEND_DEBUG
	&suspend_state.attr,
#endif
	NULL
};

static struct attribute_group tdf_attr_group = {
	.attrs = tdf_attributes,
};
/* create sysfs structure end */

static int __init td_framework_init(void)
{
	int rc = 0;

	tdf_kobject = kobject_create_and_add("td_framework", NULL);

		if (tdf_kobject)
			rc = sysfs_create_group(tdf_kobject, &tdf_attr_group);

	return rc;

}
late_initcall(td_framework_init);
