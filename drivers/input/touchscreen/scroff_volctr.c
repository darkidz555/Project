/*
 * drivers/input/touchscreen/scroff_volctr.c
 *
 * Copyright (c) 2016-2018, jollaman999 <admin@jollaman999.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/input/scroff_volctr.h>
#include <linux/input/sovc_notifier.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/fb.h>
#include <linux/hrtimer.h>
#include <asm-generic/cputime.h>

/* ******************* HOW TO WORK *******************
 * == For Volume Control ==
 *  If you swipe the touchscreen up or down in SOVC_TIME_GAP (ms)
 * time and remove your finger, volume will increase/decrease
 * just one time.
 *
 *  Otherwise if you swipe touchscreen up or down and hold your
 * finger on the touchscreen, volume will increase/decrease
 * continuously based on SOVC_VOL_REEXEC_DELAY (ms) time.
 *
 * == For Track Control ==
 *  If you swipe the touchscreen right to left in
 * SOVC_TIME_GAP (ms) time, it will play the next track.
 *
 *  Otherwise if you swipe the touchscreen left to right,
 * it will play the previous track.
 *
 *  Also if you swipe the touchscreen right or left and
 * hold your finger on touchscreen, track will change
 * continuously based on SOVC_TRACK_REEXEC_DELAY (ms) time.
 */

/* Version, author, desc, etc */
#define DRIVER_AUTHOR "jollaman999 <admin@jollaman999.com>"
#define DRIVER_DESCRIPTION "Screen Off Volume & Track Control for almost any device"
#define DRIVER_VERSION "5.0"
#define LOGTAG "[scroff_volctr]: "

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPLv2");

/* Tuneables */
#define SOVC_DEBUG			// Uncomment this to turn on the debug
#define SOVC_DEFAULT		1	// Default On/Off
#define SOVC_VOL_FEATHER	400	// Touch degree for volume control
#define SOVC_TRACK_FEATHER	500	// Touch degree for track control
#define SOVC_VOL_DETERMINE_WIDTH	250	// Width range for determine volume gestures
#define SOVC_TRACK_DETERMINE_WIDTH	300	// Width range for determine track gestures
#define SOVC_TIME_GAP		250	// Ignore touch after this time (ms)
#define SOVC_VOL_REEXEC_FIRST_DELAY	250	// First re-exec delay for volume control (ms)
#define SOVC_VOL_REEXEC_DELAY		200	// Re-exec delay for volume control (ms)
#define SOVC_VOL_REEXEC_DELAY_HIFI	110	// Re-exec delay for Hi-Fi volume control (ms)
#define SOVC_TRACK_NEXT_REEXEC_DELAY		4000	// Re-exec delay for track-next control (ms)
#define SOVC_TRACK_PREVIOUS_REEXEC_DELAY	2000	// Re-exec delay for track-previous control (ms)
#define SOVC_KEY_PRESS_DUR	100	// Key press duration (ms)
#define SOVC_VIB_STRENGTH	20	// Vibrator strength

/* Resources */
int sovc_switch = SOVC_DEFAULT;
int sovc_tmp_onoff = 0;
bool track_changed = false;
bool sovc_scr_suspended = false;
static s64 touch_time_pre_x = 0, touch_time_pre_y = 0;
static int touch_x = 0, touch_y = 0;
static int prev_x = 0, prev_y = 0;
int sovc_ignore_start_y = 0;
int sovc_ignore_end_y = 0;
bool sovc_ignore = false;
static bool is_new_touch_x = false, is_new_touch_y = false;
static bool is_executing = false;
static struct input_dev *sovc_input;
static DEFINE_MUTEX(keyworklock);
static struct workqueue_struct *sovc_volume_input_wq;
static struct workqueue_struct *sovc_track_input_wq;
static struct work_struct sovc_volume_input_work;
static struct work_struct sovc_track_input_work;

bool es9218p_playing = false;
bool tfa9872_playing = false;
bool sovc_tmp_userspace_playing = false;

bool registered = false;
static DEFINE_MUTEX(reg_lock);

struct mutex sovc_playing_state_lock;

enum CONTROL {
	NO_CONTROL,
	VOL_UP,
	VOL_DOWN,
	TRACK_NEXT,
	TRACK_PREVIOUS
};
static int control;

static void scroff_volctr_key_delayed_trigger(void);

bool sovc_hifi_mode = false;
bool sovc_state_playing(void)
{
	return 	sovc_switch & (track_changed | sovc_tmp_onoff);
}

/* Read cmdline for sovc */
static int __init read_sovc_cmdline(char *sovc)
{
	if (strcmp(sovc, "1") == 0) {
		pr_info("[cmdline_sovc]: scroff_volctr enabled. | sovc='%s'\n", sovc);
		sovc_switch = 1;
	} else if (strcmp(sovc, "0") == 0) {
		pr_info("[cmdline_sovc]: scroff_volctr disabled. | sovc='%s'\n", sovc);
		sovc_switch = 0;
	} else {
		pr_info("[cmdline_sovc]: No valid input found. Going with default: | sovc='%u'\n", sovc_switch);
	}
	return 1;
}
__setup("sovc=", read_sovc_cmdline);

/* Key work func */
static void scroff_volctr_key(struct work_struct *scroff_volctr_key_work)
{
	if (!sovc_scr_suspended || !is_executing)
		return;

	if (!mutex_trylock(&keyworklock))
		return;

	switch (control) {
	case VOL_UP:
#ifdef SOVC_DEBUG
		pr_info(LOGTAG"VOL_UP\n");
#endif
		input_event(sovc_input, EV_KEY, KEY_VOLUMEUP, 1);
		input_event(sovc_input, EV_SYN, 0, 0);
		msleep(SOVC_KEY_PRESS_DUR);
		input_event(sovc_input, EV_KEY, KEY_VOLUMEUP, 0);
		input_event(sovc_input, EV_SYN, 0, 0);
		break;
	case VOL_DOWN:
#ifdef SOVC_DEBUG
		pr_info(LOGTAG"VOL_DOWN\n");
#endif
		input_event(sovc_input, EV_KEY, KEY_VOLUMEDOWN, 1);
		input_event(sovc_input, EV_SYN, 0, 0);
		msleep(SOVC_KEY_PRESS_DUR);
		input_event(sovc_input, EV_KEY, KEY_VOLUMEDOWN, 0);
		input_event(sovc_input, EV_SYN, 0, 0);
		break;
	case TRACK_NEXT:
#ifdef SOVC_DEBUG
		pr_info(LOGTAG"TRACK_NEXT\n");
#endif
		track_changed = true;
		input_event(sovc_input, EV_KEY, KEY_NEXTSONG, 1);
		input_event(sovc_input, EV_SYN, 0, 0);
		msleep(SOVC_KEY_PRESS_DUR);
		input_event(sovc_input, EV_KEY, KEY_NEXTSONG, 0);
		input_event(sovc_input, EV_SYN, 0, 0);

		sovc_notifier_call_chain(SOVC_EVENT_TRACK_CHANGED, NULL);
		break;
	case TRACK_PREVIOUS:
#ifdef SOVC_DEBUG
		pr_info(LOGTAG"TRACK_PREVIOUS\n");
#endif
		track_changed = true;
		input_event(sovc_input, EV_KEY, KEY_PREVIOUSSONG, 1);
		input_event(sovc_input, EV_SYN, 0, 0);
		msleep(SOVC_KEY_PRESS_DUR);
		input_event(sovc_input, EV_KEY, KEY_PREVIOUSSONG, 0);
		input_event(sovc_input, EV_SYN, 0, 0);

		sovc_notifier_call_chain(SOVC_EVENT_TRACK_CHANGED, NULL);
		break;
	}

	mutex_unlock(&keyworklock);

	if (is_executing)
		scroff_volctr_key_delayed_trigger();
}
static DECLARE_DELAYED_WORK(scroff_volctr_key_work, scroff_volctr_key);

/* Key trigger */
static void scroff_volctr_key_trigger(void)
{
	queue_delayed_work(system_power_efficient_wq, &scroff_volctr_key_work, 0);
}

/* Key delayed trigger */
static void scroff_volctr_key_delayed_trigger(void)
{
	unsigned int delay;

	switch (control) {
	case VOL_UP:
	case VOL_DOWN:
		if (is_executing) {
			if (sovc_hifi_mode)
				delay = SOVC_VOL_REEXEC_DELAY_HIFI;
			else
				delay = SOVC_VOL_REEXEC_DELAY;
		} else {
			delay = SOVC_VOL_REEXEC_FIRST_DELAY;
		}
		break;
	case TRACK_NEXT:
		delay = SOVC_TRACK_NEXT_REEXEC_DELAY;
		break;
	case TRACK_PREVIOUS:
		delay = SOVC_TRACK_PREVIOUS_REEXEC_DELAY;
		break;
	}

	queue_delayed_work(system_power_efficient_wq, &scroff_volctr_key_work,
			   msecs_to_jiffies(delay));
}

/* reset on finger release */
static void scroff_volctr_reset(void)
{
	is_executing = false;
	is_new_touch_x = false;
	is_new_touch_y = false;
	control = NO_CONTROL;
}

/* init a new touch */
static void new_touch_x(int x)
{
	touch_time_pre_x = ktime_to_ms(ktime_get());
	is_new_touch_x = true;
	prev_x = x;
}

unsigned int calc_feather(int coord, int prev_coord)
{
	int calc_coord = 0;

	calc_coord = coord - prev_coord;
	if (calc_coord < 0)
		calc_coord *= -1;
	return calc_coord;
}

static void new_touch_y(int y)
{
	touch_time_pre_y = ktime_to_ms(ktime_get());
	is_new_touch_y = true;
	prev_y = y;
}

/* exec key control */
static void exec_key(int key)
{
	is_executing = true;
	control = key;
	scroff_volctr_key_trigger();
}

/* scroff_volctr volume function */
static void sovc_volume_input_callback(struct work_struct *unused)
{
	s64 time;

	if (!is_new_touch_y)
		new_touch_y(touch_y);

	time = ktime_to_ms(ktime_get()) - touch_time_pre_y;

	if (time > 0 && time < SOVC_TIME_GAP &&
	    calc_feather(touch_x, prev_x) < SOVC_VOL_DETERMINE_WIDTH) {
		if (prev_y - touch_y > SOVC_VOL_FEATHER) // Volume Up (down->up)
			exec_key(VOL_UP);
		else if (touch_y - prev_y > SOVC_VOL_FEATHER) // Volume Down (up->down)
			exec_key(VOL_DOWN);
	}
}

/* scroff_volctr track function */
static void sovc_track_input_callback(struct work_struct *unused)
{
	s64 time;

	if (!is_new_touch_x)
		new_touch_x(touch_x);

	time = ktime_to_ms(ktime_get()) - touch_time_pre_x;

	if (time > 0 && time < SOVC_TIME_GAP &&
	    calc_feather(touch_y, prev_y) < SOVC_TRACK_DETERMINE_WIDTH) {
		if (prev_x - touch_x > SOVC_TRACK_FEATHER) // Track Next (right->left)
			exec_key(TRACK_NEXT);
		else if (touch_x - prev_x > SOVC_TRACK_FEATHER) // Track Previous (left->right)
			exec_key(TRACK_PREVIOUS);
	}
}

static int sovc_input_common_event(struct input_handle *handle, unsigned int type,
				unsigned int code, int value)
{
	if (!sovc_switch || !sovc_scr_suspended || !sovc_tmp_onoff)
		return 1;

	/* You can debug here with 'adb shell getevent -l' command. */
	switch(code) {
		case ABS_MT_SLOT:
			scroff_volctr_reset();
			return 1;

		case ABS_MT_TRACKING_ID:
			if (value == 0xffffffff)
				scroff_volctr_reset();
			return 1;
		default:
			break;
	}

	if (is_executing || sovc_ignore)
		return 1;

	return 0;
}

static void sovc_volume_input_event(struct input_handle *handle, unsigned int type,
				unsigned int code, int value)
{
	int out;

	out = sovc_input_common_event(handle, type, code, value);
	if (out)
		return;

	/* You can debug here with 'adb shell getevent -l' command. */
	switch(code) {
		case ABS_MT_POSITION_Y:
			touch_y = value;
			queue_work(sovc_volume_input_wq, &sovc_volume_input_work);
			break;

		default:
			break;
	}
}

static void sovc_track_input_event(struct input_handle *handle, unsigned int type,
				unsigned int code, int value)
{
	int out;

	out = sovc_input_common_event(handle, type, code, value);
	if (out)
		return;

	/* You can debug here with 'adb shell getevent -l' command. */
	switch(code) {
		case ABS_MT_POSITION_X:
			touch_x = value;
			queue_work(sovc_track_input_wq, &sovc_track_input_work);
			break;

		default:
			break;
	}
}

static int input_dev_filter(struct input_dev *dev)
{
	if (strstr(dev->name, "touch_dev"))
		return 0;
	else
		return 1;
}

static int sovc_input_connect(struct input_handler *handler,
				struct input_dev *dev, const struct input_device_id *id,
				char *handle_name)
{
	struct input_handle *handle;
	int error;

	if (input_dev_filter(dev))
		return -ENODEV;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = handle_name;

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static int sovc_volume_input_connect(struct input_handler *handler,
				struct input_dev *dev, const struct input_device_id *id)
{
	return sovc_input_connect(handler, dev, id, "sovc_volume");
}

static int sovc_track_input_connect(struct input_handler *handler,
				struct input_dev *dev, const struct input_device_id *id)
{
	return sovc_input_connect(handler, dev, id, "sovc_track");
}

static void sovc_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id sovc_ids[] = {
	{ .driver_info = 1 },
	{ },
};

static struct input_handler sovc_volume_input_handler = {
	.event		= sovc_volume_input_event,
	.connect	= sovc_volume_input_connect,
	.disconnect	= sovc_input_disconnect,
	.name		= "sovc_volume_inputreq",
	.id_table	= sovc_ids,
};

static struct input_handler sovc_track_input_handler = {
	.event		= sovc_track_input_event,
	.connect	= sovc_track_input_connect,
	.disconnect	= sovc_input_disconnect,
	.name		= "sovc_track_inputreq",
	.id_table	= sovc_ids,
};

static int register_sovc(void)
{
	int rc = 0;

	mutex_lock(&reg_lock);

	if (registered) {
#ifdef SOVC_DEBUG
		pr_info(LOGTAG"%s already registered\n", __func__);
#endif
		goto out;
	}

	scroff_volctr_reset();

	sovc_volume_input_wq = create_workqueue("sovc_volume_iwq");
	if (!sovc_volume_input_wq) {
		pr_err("%s: Failed to create sovc_volume_iwq workqueue\n", __func__);
		mutex_unlock(&reg_lock);
		return -EFAULT;
	}
	INIT_WORK(&sovc_volume_input_work, sovc_volume_input_callback);
	sovc_track_input_wq = create_workqueue("sovc_track_iwq");
	if (!sovc_track_input_wq) {
		pr_err("%s: Failed to create sovc_track_iwq workqueue\n", __func__);
		mutex_unlock(&reg_lock);
		return -EFAULT;
	}
	INIT_WORK(&sovc_track_input_work, sovc_track_input_callback);

	rc = input_register_handler(&sovc_volume_input_handler);
	if (rc) {
		pr_err("%s: Failed to register sovc_volume_input_handler\n", __func__);
		goto err;
	}
	rc = input_register_handler(&sovc_track_input_handler);
	if (rc) {
		pr_err("%s: Failed to register sovc_track_input_handler\n", __func__);
		goto err;
	}

	registered = true;
out:
	mutex_unlock(&reg_lock);
#ifdef SOVC_DEBUG
	pr_info(LOGTAG"%s done\n", __func__);
#endif

	return rc;
err:
	flush_workqueue(sovc_volume_input_wq);
	flush_workqueue(sovc_track_input_wq);
	destroy_workqueue(sovc_volume_input_wq);
	destroy_workqueue(sovc_track_input_wq);
	cancel_work_sync(&sovc_volume_input_work);
	cancel_work_sync(&sovc_track_input_work);
	mutex_unlock(&reg_lock);

	return rc;
}

void unregister_sovc(void)
{
	mutex_lock(&reg_lock);

	if(!registered) {
#ifdef SOVC_DEBUG
		pr_info(LOGTAG"%s already unregistered\n", __func__);
#endif
		goto out;
	}

	scroff_volctr_reset();

	input_unregister_handler(&sovc_volume_input_handler);
	input_unregister_handler(&sovc_track_input_handler);
	flush_workqueue(sovc_volume_input_wq);
	flush_workqueue(sovc_track_input_wq);
	destroy_workqueue(sovc_volume_input_wq);
	destroy_workqueue(sovc_track_input_wq);
	cancel_work_sync(&sovc_volume_input_work);
	cancel_work_sync(&sovc_track_input_work);

	registered = false;
out:
	mutex_unlock(&reg_lock);
#ifdef SOVC_DEBUG
	pr_info(LOGTAG"%s done\n", __func__);
#endif
}

/*
 * SYSFS stuff below here
 */
static ssize_t sovc_scroff_volctr_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", sovc_switch);

	return count;
}

static ssize_t sovc_scroff_volctr_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int rc, val;

	rc = kstrtoint(buf, 10, &val);
	if (rc)
		return -EINVAL;

	if (val == 0 || val == 1) {
		if (sovc_switch != val)
			sovc_switch = val;
	} else
		return -EINVAL;

	if (sovc_switch && sovc_tmp_onoff && sovc_scr_suspended)
		register_sovc();
	else
		unregister_sovc();

	return count;
}

static DEVICE_ATTR(scroff_volctr, (S_IWUSR|S_IRUGO),
	sovc_scroff_volctr_show, sovc_scroff_volctr_dump);

static ssize_t sovc_scroff_volctr_temp_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", sovc_tmp_onoff);

	return count;
}

static ssize_t sovc_scroff_volctr_temp_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	int rc, val;

	rc = kstrtoint(buf, 10, &val);
	if (rc)
		return -EINVAL;

	if (val == 0 || val == 1) {
		if (val) {
			sovc_tmp_userspace_playing = true;
			if (sovc_switch && !sovc_tmp_onoff) {
				mutex_lock(&sovc_playing_state_lock);
				sovc_notifier_call_chain(SOVC_EVENT_PLAYING, NULL);
				mutex_unlock(&sovc_playing_state_lock);
			}
		} else {
			sovc_tmp_userspace_playing = false;
			if (sovc_switch && sovc_tmp_onoff && !es9218p_playing && !tfa9872_playing) {
				mutex_lock(&sovc_playing_state_lock);
				sovc_notifier_call_chain(SOVC_EVENT_STOPPED, NULL);
				mutex_unlock(&sovc_playing_state_lock);
			}
		}
	}

	return count;
}

static DEVICE_ATTR(scroff_volctr_temp, (S_IWUSR|S_IRUGO),
	sovc_scroff_volctr_temp_show, sovc_scroff_volctr_temp_dump);

static ssize_t sovc_version_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%s\n", DRIVER_VERSION);

	return count;
}

static ssize_t sovc_version_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static DEVICE_ATTR(scroff_volctr_version, (S_IWUSR|S_IRUGO),
	sovc_version_show, sovc_version_dump);

static int sovc_fb_notifier_callback(struct notifier_block *self,
				unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;

	if (!sovc_switch)
		return 0;

	if (event == FB_EVENT_BLANK) {
		blank = evdata->data;

		if (*blank == FB_BLANK_UNBLANK) {
			sovc_scr_suspended = false;
			track_changed = false;
			unregister_sovc();
			break;
		} else {
			if (sovc_scr_suspended)
				return 0;
			sovc_scr_suspended = true;
			if (sovc_state_playing())
				register_sovc();
			break;
		}
	}

	return 0;
}

struct notifier_block sovc_fb_notif = {
	.notifier_call = sovc_fb_notifier_callback,
};

static int sovc_notifier_callback(struct notifier_block *self,
				unsigned long event, void *data)
{
	if (!sovc_switch)
		return 0;

	switch (event) {
	case SOVC_EVENT_PLAYING:
#ifdef SOVC_DEBUG
		pr_info(LOGTAG"SOVC_EVENT: Playing\n");
#endif
		track_changed = false;
		sovc_tmp_onoff = 1;
		break;
	case SOVC_EVENT_STOPPED:
#ifdef SOVC_DEBUG
		pr_info(LOGTAG"SOVC_EVENT: Stopped\n");
#endif
		if (!track_changed)
			sovc_tmp_onoff = 0;
		break;
	}

	return 0;
}

struct notifier_block sovc_notif = {
	.notifier_call = sovc_notifier_callback,
};

/*
 * INIT / EXIT stuff below here
 */
struct kobject *android_touch_kobj;

static int __init scroff_volctr_init(void)
{
	int rc = 0;

	sovc_input = input_allocate_device();
	if (!sovc_input) {
		pr_err("Can't allocate scroff_volctr input device!\n");
		goto err_alloc_dev;
	}

	input_set_capability(sovc_input, EV_KEY, KEY_VOLUMEUP);
	input_set_capability(sovc_input, EV_KEY, KEY_VOLUMEDOWN);
	input_set_capability(sovc_input, EV_KEY, KEY_NEXTSONG);
	input_set_capability(sovc_input, EV_KEY, KEY_PREVIOUSSONG);
	sovc_input->name = "sovc_input";
	sovc_input->phys = "sovc_input/input0";

	rc = input_register_device(sovc_input);
	if (rc) {
		pr_err("%s: input_register_device err=%d\n", __func__, rc);
		goto err_input_dev;
	}

	android_touch_kobj = kobject_create_and_add("android_touch", NULL) ;
	if (android_touch_kobj == NULL) {
		pr_warn("%s: android_touch_kobj create_and_add failed\n", __func__);
	}

	rc = fb_register_client(&sovc_fb_notif);
	if (rc) {
		pr_warn("%s: fb register failed\n", __func__);
	}
	rc = sovc_register_client(&sovc_notif);
	if (rc) {
		pr_warn("%s: sovc notifier register failed\n", __func__);
	}

	rc = sysfs_create_file(android_touch_kobj, &dev_attr_scroff_volctr.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for scroff_volctr\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_scroff_volctr_temp.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for scroff_volctr_temp\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_scroff_volctr_version.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for scroff_volctr_version\n", __func__);
	}

	mutex_init(&sovc_playing_state_lock);

err_input_dev:
	input_free_device(sovc_input);
err_alloc_dev:
	pr_info(LOGTAG"%s done\n", __func__);

	return 0;
}

static void __exit scroff_volctr_exit(void)
{
	kobject_del(android_touch_kobj);
	unregister_sovc();
	input_unregister_device(sovc_input);
	input_free_device(sovc_input);
	fb_unregister_client(&sovc_fb_notif);
	sovc_unregister_client(&sovc_notif);
}

module_init(scroff_volctr_init);
module_exit(scroff_volctr_exit);
