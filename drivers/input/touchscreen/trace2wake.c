/*
 * drivers/input/touchscreen/trace2wake.c
 *
 *
 * Copyright (c) 2017, Tanish <tanish2k09.dev@gmail.com>
 *
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
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/hrtimer.h>
#include <linux/input/trace2wake.h>

#ifdef CONFIG_POCKETMOD
#include <linux/pocket_mod.h>
#endif

#define WAKE_HOOKS_DEFINED

#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
#include <linux/lcd_notify.h>
#else
#include <linux/earlysuspend.h>
#endif
#endif

/* uncomment since no touchscreen defines android touch, do that here */
//#define ANDROID_TOUCH_DECLARED

/* Version, author, desc, etc */
#define DRIVER_AUTHOR "Tanish <tanish2k09.dev@gmail.com>"
#define DRIVER_DESCRIPTION "Trace2wake for almost any device"
#define DRIVER_VERSION "2.0"
#define LOGTAG "[trace2wake]: "

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPLv3");

#ifdef CONFIG_TOUCHSCREEN_SWEEP2WAKE
#define ANDROID_TOUCH_DECLARED
#endif

/* Tuneables */
#define T2W_DEBUG             0
#define T2W_DEFAULT           1
#define T2W_MULTITOUCH_SLEEP  2
#define T2W_PWRKEY_DUR       60

/* We are assuming the device to be 1080p res... */
#define X_HALF 540      // Value of the half of width of screen
#define Y_MAX 1920      // Value of the full height of screen
#define LOWER_BOUND_RADIUS 390
#define UPPER_BOUND_RADIUS 630
#define Y_INTERCEPT_AT_SIDES 1595    //This value must be calculated by the one who is compiling.
//To calculate y_intercept, here is the formula :
// Y_INTERCEPT_AT_SIDES = Y_MAX - (sq_root_of((UPPER_BOUND_RADIUS*UPPER_BOUND_RADIUS) - (X_HALF*X_HALF)))
// Predefining it helps us in avoiding calculating it in every call.

/* For 720p devices, use the following config and comment out above config */
/*
#define X_HALF 360      // Value of the half of width of screen
#define Y_MAX 1280      // Value of the full height of screen
#define LOWER_BOUND_RADIUS 260
#define UPPER_BOUND_RADIUS 420
#define Y_INTERCEPT_AT_SIDES 1064
*/


/* Resources */
int t2w_switch = T2W_DEFAULT;
static int touch_x = 0, touch_y = 0, init_x=-1, init_y=-1, init=-1;
static bool touch_x_called = false, touch_y_called = false;
static bool exec_count = true, checkpoint = false;
bool t2w_scr_suspended = false;
#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
static struct notifier_block t2w_lcd_notif;
#endif
#endif
static struct input_dev * trace2wake_pwrdev;
static DEFINE_MUTEX(pwrkeyworklock);
static struct workqueue_struct *t2w_input_wq;
static struct work_struct t2w_input_work;

/* PowerKey setter */
void trace2wake_setdev(struct input_dev * input_device) {
	trace2wake_pwrdev = input_device;
	printk(LOGTAG"set trace2wake_pwrdev: %s\n", trace2wake_pwrdev->name);
}

/* Read cmdline for t2w */
static int __init read_t2w_cmdline(char *t2w)
{
	if (strcmp(t2w, "1") == 0) {
		pr_info("[cmdline_t2w]: Trace2Wake enabled. | t2w='%s'\n", t2w);
		t2w_switch = 1;
	} else if (strcmp(t2w, "0") == 0) {
		pr_info("[cmdline_t2w]: Trace2Wake disabled. | t2w='%s'\n", t2w);
		t2w_switch = 0;
	} else if (strcmp(t2w, "2") == 0) {
		pr_info("[cmdline_t2w]: Trace2Wake with multi_touch enabled. | t2w='%s'\n", t2w);
		t2w_switch = 2;
	} else {
		pr_info("[cmdline_t2w]: No valid input found. Going with default: | t2w='%u'\n", t2w_switch);
	}
	return 1;
}
__setup("t2w=", read_t2w_cmdline);

/* PowerKey work func */
static void trace2wake_presspwr(struct work_struct * trace2wake_presspwr_work) {
	if (!mutex_trylock(&pwrkeyworklock))
		return;
	input_event(trace2wake_pwrdev, EV_KEY, KEY_POWER, 1);
	input_event(trace2wake_pwrdev, EV_SYN, 0, 0);
	msleep(T2W_PWRKEY_DUR);
	input_event(trace2wake_pwrdev, EV_KEY, KEY_POWER, 0);
	input_event(trace2wake_pwrdev, EV_SYN, 0, 0);
	msleep(T2W_PWRKEY_DUR);
	mutex_unlock(&pwrkeyworklock);
	return;
}
static DECLARE_WORK(trace2wake_presspwr_work, trace2wake_presspwr);

/* PowerKey trigger */
static void trace2wake_pwrtrigger(void) {
	    schedule_work(&trace2wake_presspwr_work);
	return;
}

/* reset on finger release */
static void trace2wake_reset(void) {
	exec_count = true;
    init = -1;
    init_x = -1;
    init_y = -1;
    checkpoint = 0;
}

/* Trace2wake main functions */
static void detect_trace2wake_left(int x, int y)
{
    int circle_x = (x - X_HALF) * (x - X_HALF);
	int circle_y = circle_x + ((y - Y_MAX) * (y - Y_MAX));

    if (t2w_switch == 1)
    {
        if ((x > (5*X_HALF/6)) && (x < (7*X_HALF/6)))
			checkpoint = 1;
	
	    if (!((circle_y > (LOWER_BOUND_RADIUS*LOWER_BOUND_RADIUS)) && (circle_y < (UPPER_BOUND_RADIUS*UPPER_BOUND_RADIUS))))
	    {
		    trace2wake_reset();
	    }
	    else if ((x > ((X_HALF*5)/3)) && (y > Y_INTERCEPT_AT_SIDES) && (exec_count) && (checkpoint))
	    {
		    trace2wake_pwrtrigger();
		    exec_count = false;
	    }
    }
    else if (t2w_switch == 2)
    {
        if (!((circle_y > (LOWER_BOUND_RADIUS*LOWER_BOUND_RADIUS)) && (circle_y < (UPPER_BOUND_RADIUS*UPPER_BOUND_RADIUS))))
	        {
		        trace2wake_reset();
	        }
	        else if ((x > ((X_HALF*5)/3)) && (y > Y_INTERCEPT_AT_SIDES) && (exec_count))
	        {
		        trace2wake_pwrtrigger();
		        exec_count = false;
	        }
    }
	
	return;
}

static void detect_trace2wake_right(int x, int y)
{
    int circle_x = (x - X_HALF) * (x - X_HALF);
	int circle_y = circle_x + ((y - Y_MAX) * (y - Y_MAX));

    if (t2w_switch == 1)
    {
        if ((x > (5*X_HALF/6)) && (x < (7*X_HALF/6)))
			checkpoint = 1;
	
	    if (!((circle_y > (LOWER_BOUND_RADIUS*LOWER_BOUND_RADIUS)) && (circle_y < (UPPER_BOUND_RADIUS*UPPER_BOUND_RADIUS))))
	    {
		    trace2wake_reset();
	    }
	    else if ((x < (X_HALF/3)) && (y > Y_INTERCEPT_AT_SIDES) && (exec_count) && (checkpoint))
	    {
		    trace2wake_pwrtrigger();
		    exec_count = false;
	    }
    }
    else if (t2w_switch == 2)
    {
        if (!((circle_y > (LOWER_BOUND_RADIUS*LOWER_BOUND_RADIUS)) && (circle_y < (UPPER_BOUND_RADIUS*UPPER_BOUND_RADIUS))))
	        {
		        trace2wake_reset();
	        }
	        else if ((x < (X_HALF/3)) && (y > Y_INTERCEPT_AT_SIDES) && (exec_count) && (checkpoint))
	        {
		        trace2wake_pwrtrigger();
		        exec_count = false;
	        }
    }
	
	return;
}

static void t2w_input_callback(struct work_struct *unused) {
	if (((!t2w_scr_suspended) && (touch_y < Y_MAX)) && (t2w_switch != 0))
	{
		if (init == -1)
		{
			init = 0;
			init_x = touch_x;
			init_y = touch_y;
		}
		if ((init_x < (X_HALF/3) ) && (init_y > Y_INTERCEPT_AT_SIDES))
			detect_trace2wake_left(touch_x, touch_y);
		else if ((init_x > ((X_HALF*5)/3) ) && (init_y > Y_INTERCEPT_AT_SIDES))
			detect_trace2wake_right(touch_x, touch_y);
	}

	return;
}

static void t2w_input_event(struct input_handle *handle, unsigned int type,
				unsigned int code, int value) {
#if T2W_DEBUG
	pr_info("trace2wake: code: %s|%u, val: %i\n",
		((code==ABS_MT_POSITION_X) ? "X" :
		(code==ABS_MT_POSITION_Y) ? "Y" :
		((code==ABS_MT_TRACKING_ID)||
			(code==330)) ? "ID" : "undef"), code, value);
#endif

	if ((t2w_scr_suspended)){	
    	return;
    }


	if (code == ABS_MT_SLOT) {
		trace2wake_reset();
		return;
	}

	/*
	 * '330'? Many touch panels are 'broken' in the sense of not following the
	 * multi-touch protocol given in Documentation/input/multi-touch-protocol.txt.
	 * According to the docs, touch panels using the type B protocol must send in
	 * a ABS_MT_TRACKING_ID event after lifting the contact in the first slot.
	 * This should in the flow of events, help us reset the trace2wake variables
	 * and proceed as per the algorithm.
	 *
	 * This however is not the case with various touch panel drivers, and hence
	 * there is no reliable way of tracking ABS_MT_TRACKING_ID on such panels.
	 * Some of the panels however do track the lifting of contact, but with a
	 * different event code, and a different event value.
	 *
	 * So, add checks for those event codes and values to keep the algo flow.
	 *
	 * synaptics_s3203 => code: 330; val: 0
	 *
	 * Note however that this is not possible with panels like the CYTTSP3 panel
	 * where there are no such events being reported for the lifting of contacts
	 * though i2c data has a ABS_MT_TRACKING_ID or equivalent event variable
	 * present. In such a case, make sure the trace2wake_reset() function is
	 * publicly available for external calls.
	 *
	 */
	if ((code == ABS_MT_TRACKING_ID && value == -1) ||
		(code == 330 && value == 0)) {
		trace2wake_reset();
		return;
	}

	if (code == ABS_MT_POSITION_X) {
		touch_x = value;
		touch_x_called = true;
	}

	if (code == ABS_MT_POSITION_Y) {
		touch_y = value;
		touch_y_called = true;
	}

	if (touch_x_called && touch_y_called) {
		touch_x_called = false;
		touch_y_called = false;
		queue_work_on(0, t2w_input_wq, &t2w_input_work);
	}
}

static int input_dev_filter(struct input_dev *dev) {
	if (strstr(dev->name, "touch")||
			strstr(dev->name, "mtk-tpd")) {
		return 0;
	} else {
		return 1;
	}
}

static int t2w_input_connect(struct input_handler *handler,
				struct input_dev *dev, const struct input_device_id *id) {
	struct input_handle *handle;
	int error;

	if (input_dev_filter(dev))
		return -ENODEV;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "t2w";

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

static void t2w_input_disconnect(struct input_handle *handle) {
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id t2w_ids[] = {
	{ .driver_info = 1 },
	{ },
};

static struct input_handler t2w_input_handler = {
	.event		= t2w_input_event,
	.connect	= t2w_input_connect,
	.disconnect	= t2w_input_disconnect,
	.name		= "t2w_inputreq",
	.id_table	= t2w_ids,
};

#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
static int lcd_notifier_callback(struct notifier_block *this,
				unsigned long event, void *data)
{
	switch (event) {
	case LCD_EVENT_ON_END:
		t2w_scr_suspended = false;
		break;
	case LCD_EVENT_OFF_END:
		t2w_scr_suspended = true;
		break;
	default:
		break;
	}

	return 0;
}
#else
static void t2w_early_suspend(struct early_suspend *h) {
	t2w_scr_suspended = true;
}

static void t2w_late_resume(struct early_suspend *h) {
	t2w_scr_suspended = false;
}

static struct early_suspend t2w_early_suspend_handler = {
	.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN,
	.suspend = t2w_early_suspend,
	.resume = t2w_late_resume,
};
#endif
#endif

/*
 * SYSFS stuff below here
 */
static ssize_t t2w_trace2wake_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%d\n", t2w_switch);

	return count;
}

static ssize_t t2w_trace2wake_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	if (buf[1] == '\n') {
		if (buf[0] == '0') {
			t2w_switch = 0;
		} else if (buf[0] == '1') {
			t2w_switch = 1;
		} else if (buf[0] == '2') {
            t2w_switch = 2;
        }
        
	}

	return count;
}

static DEVICE_ATTR(trace2wake, (S_IWUSR|S_IRUGO),
	t2w_trace2wake_show, t2w_trace2wake_dump);

static ssize_t t2w_version_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t count = 0;

	count += sprintf(buf, "%s\n", DRIVER_VERSION);

	return count;
}

static ssize_t t2w_version_dump(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

static DEVICE_ATTR(trace2wake_version, (S_IWUSR|S_IRUGO),
	t2w_version_show, t2w_version_dump);

/*
 * INIT / EXIT stuff below here
 */
#ifdef ANDROID_TOUCH_DECLARED
extern struct kobject *android_touch_kobj;
#else
struct kobject *android_touch_kobj;
EXPORT_SYMBOL_GPL(android_touch_kobj);
#endif
static int __init trace2wake_init(void)
{
	int rc = 0;

	t2w_input_wq = create_workqueue("t2wiwq");
	if (!t2w_input_wq) {
		pr_err("%s: Failed to create t2wiwq workqueue\n", __func__);
		return -EFAULT;
	}
	INIT_WORK(&t2w_input_work, t2w_input_callback);
	rc = input_register_handler(&t2w_input_handler);
	if (rc)
		pr_err("%s: Failed to register t2w_input_handler\n", __func__);

#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
	t2w_lcd_notif.notifier_call = lcd_notifier_callback;
	if (lcd_register_client(&t2w_lcd_notif) != 0) {
		pr_err("%s: Failed to register lcd callback\n", __func__);
	}
#else
	register_early_suspend(&t2w_early_suspend_handler);
#endif
#endif

#ifndef ANDROID_TOUCH_DECLARED
	android_touch_kobj = kobject_create_and_add("android_touch", NULL) ;
	if (android_touch_kobj == NULL) {
		pr_warn("%s: android_touch_kobj create_and_add failed\n", __func__);
	}
#endif
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_trace2wake.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for trace2wake\n", __func__);
	}
	rc = sysfs_create_file(android_touch_kobj, &dev_attr_trace2wake_version.attr);
	if (rc) {
		pr_warn("%s: sysfs_create_file failed for trace2wake_version\n", __func__);
	}

	return 0;
}

static void __exit trace2wake_exit(void)
{
#ifndef ANDROID_TOUCH_DECLARED
	kobject_del(android_touch_kobj);
#endif
#ifndef WAKE_HOOKS_DEFINED
#ifndef CONFIG_HAS_EARLYSUSPEND
	lcd_unregister_client(&t2w_lcd_notif);
#endif
#endif
	input_unregister_handler(&t2w_input_handler);
	destroy_workqueue(t2w_input_wq);
	return;
}

module_init(trace2wake_init);
module_exit(trace2wake_exit);
