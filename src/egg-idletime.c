/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <X11/Xlib.h>
#include <X11/extensions/sync.h>
#include <gdk/gdkx.h>
#include <gdk/gdk.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "egg-idletime.h"

static void     egg_idletime_finalize   (GObject       *object);

#define EGG_IDLETIME_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), EGG_IDLETIME_TYPE, EggIdletimePrivate))

struct EggIdletimePrivate
{
	int			 sync_event;
	guint			 last_event;
	gboolean		 reset_set;
	XSyncCounter		 idle_counter;
	GPtrArray		*array;
	Display			*dpy;
};

typedef struct
{
	guint			 id;
	XSyncValue		 timeout;
	XSyncAlarm		 xalarm;
} EggIdletimeAlarm;

enum {
	SIGNAL_ALARM_EXPIRED,
	SIGNAL_RESET,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0 };
static gpointer egg_idletime_object = NULL;

G_DEFINE_TYPE (EggIdletime, egg_idletime, G_TYPE_OBJECT)

/**
 * egg_idletime_xsync_alarm_set:
 *
 * Gets the time remaining for the current percentage
 *
 */
static void
egg_idletime_xsync_alarm_set (EggIdletime *idletime, EggIdletimeAlarm *alarm, gboolean positive)
{
	XSyncAlarmAttributes attr;
	XSyncValue delta;
	unsigned int flags;
	XSyncTestType test;

	/* which way do we do the test? */
	if (positive)
		test = XSyncPositiveComparison;
	else
		test = XSyncNegativeComparison;

	XSyncIntToValue (&delta, 0);

	attr.trigger.counter = idletime->priv->idle_counter;
	attr.trigger.value_type = XSyncAbsolute;
	attr.trigger.test_type = test;
	attr.trigger.wait_value = alarm->timeout;
	attr.delta = delta;

	flags = XSyncCACounter | XSyncCAValueType | XSyncCATestType | XSyncCAValue | XSyncCADelta;

	if (alarm->xalarm)
		XSyncChangeAlarm (idletime->priv->dpy, alarm->xalarm, flags, &attr);
	else
		alarm->xalarm = XSyncCreateAlarm (idletime->priv->dpy, flags, &attr);
}

/**
 * egg_idletime_alarm_reset_all:
 */
void
egg_idletime_alarm_reset_all (EggIdletime *idletime)
{
	guint i;
	EggIdletimeAlarm *alarm;

	/* reset all the alarms (except the reset alarm) to their timeouts */
	for (i=1; i<idletime->priv->array->len; i++) {
		alarm = g_ptr_array_index (idletime->priv->array, i);
		egg_idletime_xsync_alarm_set (idletime, alarm, TRUE);
	}

	/* emit signal */
	g_signal_emit (idletime, signals [SIGNAL_RESET], 0);

	/* we need to be reset again on the next event */
	idletime->priv->reset_set = FALSE;
}

/**
 * egg_idletime_timeout:
 */
static void
egg_idletime_timeout (EggIdletime *idletime, EggIdletimeAlarm *alarm)
{
	/* emit signal */
	g_signal_emit (idletime, signals [SIGNAL_ALARM_EXPIRED], 0, alarm->id);
}

/**
 * egg_idletime_xsync_value_add_one:
 *
 * Just adds one to a XSyncValue. I love X.
 */
static void
egg_idletime_xsync_value_add_one (XSyncValue *from, XSyncValue *to)
{
	int overflow;
	XSyncValue add;
	XSyncIntToValue (&add, -1);
	XSyncValueAdd (to, *from, add, &overflow);
}

/**
 * egg_idletime_alarm_find_id:
 */
static EggIdletimeAlarm *
egg_idletime_alarm_find_id (EggIdletime *idletime, guint id)
{
	guint i;
	EggIdletimeAlarm *alarm;
	for (i=0; i<idletime->priv->array->len; i++) {
		alarm = g_ptr_array_index (idletime->priv->array, i);
		if (alarm->id == id)
			return alarm;
	}
	return NULL;
}

/**
 * egg_idletime_x_set_reset:
 */
static void
egg_idletime_x_set_reset (EggIdletime *idletime, XSyncAlarmNotifyEvent *alarm_event)
{
	EggIdletimeAlarm *alarm;

	alarm = egg_idletime_alarm_find_id (idletime, 0);

	if (idletime->priv->reset_set == FALSE) {
		/* don't match on the current value because
		 * XSyncNegativeComparison means less or equal. */
		egg_idletime_xsync_value_add_one (&alarm_event->counter_value, &alarm->timeout);

		/* set the reset alarm to fire the next time
		 * idletime->priv->idle_counter < the current counter value */
		egg_idletime_xsync_alarm_set (idletime, alarm, FALSE);

		/* don't try to set this again */
		idletime->priv->reset_set = TRUE;
	}
}

/**
 * egg_idletime_alarm_find_event:
 */
static EggIdletimeAlarm *
egg_idletime_alarm_find_event (EggIdletime *idletime, XSyncAlarmNotifyEvent *alarm_event)
{
	guint i;
	EggIdletimeAlarm *alarm;
	for (i=0; i<idletime->priv->array->len; i++) {
		alarm = g_ptr_array_index (idletime->priv->array, i);
		if (alarm_event->alarm == alarm->xalarm)
			return alarm;
	}
	return NULL;
}

/**
 * egg_idletime_x_event_filter:
 */
static GdkFilterReturn
egg_idletime_x_event_filter (GdkXEvent *gdkxevent, GdkEvent *event, gpointer data)
{
	EggIdletimeAlarm *alarm;
	XEvent *xevent = (XEvent *) gdkxevent;
	EggIdletime *idletime = (EggIdletime *) data;
	XSyncAlarmNotifyEvent *alarm_event;

	/* no point continuing */
	if (xevent->type != idletime->priv->sync_event + XSyncAlarmNotify)
		return GDK_FILTER_CONTINUE;

	alarm_event = (XSyncAlarmNotifyEvent *) xevent;

	alarm = egg_idletime_alarm_find_event (idletime, alarm_event);

	/* did we match one of our alarms? */
	if (alarm != NULL) {
		/* save the last state we triggered */
		idletime->priv->last_event = alarm->id;

		/* do the signal */
		if (alarm->id != 0) {
			egg_idletime_timeout (idletime, alarm);

			/* we need the first alarm to go off to set the reset alarm */
			egg_idletime_x_set_reset (idletime, alarm_event);
			return GDK_FILTER_CONTINUE;
		}

		/* do the reset callback */
		egg_idletime_alarm_reset_all (idletime);
	}

	return GDK_FILTER_CONTINUE;
}

/**
 * egg_idletime_get_idletime->priv->last_event:
 */
guint
egg_idletime_alarm_get (EggIdletime *idletime)
{
	return idletime->priv->last_event;
}

/**
 * egg_idletime_alarm_set:
 */
gboolean
egg_idletime_alarm_set (EggIdletime *idletime, guint id, guint timeout)
{
	EggIdletimeAlarm *alarm;

	/* id cannot be zero */
	if (id == 0)
		return FALSE;

	/* timeout cannot be zero */
	if (timeout == 0)
		return FALSE;

	/* see if we already created an alarm with this ID */
	alarm = egg_idletime_alarm_find_id (idletime, id);
	if (alarm == NULL) {
		/* create a new alarm */
		alarm = g_new0 (EggIdletimeAlarm, 1);

		/* set the id - this is just something like userdata */
		alarm->id = id;

		/* add to array */
		g_ptr_array_add (idletime->priv->array, alarm);
	}

	/* set the timeout */
	XSyncIntToValue (&alarm->timeout, (gint)timeout);

	/* set, and start the timer */
	egg_idletime_xsync_alarm_set (idletime, alarm, TRUE);
	return TRUE;
}

/**
 * egg_idletime_alarm_free:
 */
static gboolean
egg_idletime_alarm_free (EggIdletime *idletime, EggIdletimeAlarm *alarm)
{
	XSyncDestroyAlarm (idletime->priv->dpy, alarm->xalarm);
	g_free (alarm);
	g_ptr_array_remove (idletime->priv->array, alarm);
	return TRUE;
}

/**
 * egg_idletime_alarm_free:
 */
gboolean
egg_idletime_alarm_remove (EggIdletime *idletime, guint id)
{
	EggIdletimeAlarm *alarm;
	alarm = egg_idletime_alarm_find_id (idletime, id);
	if (alarm == NULL)
		return FALSE;
	egg_idletime_alarm_free (idletime, alarm);
	return TRUE;
}

/**
 * egg_idletime_class_init:
 * @klass: This class instance
 **/
static void
egg_idletime_class_init (EggIdletimeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = egg_idletime_finalize;
	g_type_class_add_private (klass, sizeof (EggIdletimePrivate));

	signals [SIGNAL_ALARM_EXPIRED] =
		g_signal_new ("alarm-expired",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EggIdletimeClass, alarm_expired),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [SIGNAL_RESET] =
		g_signal_new ("reset",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (EggIdletimeClass, reset),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * egg_idletime_init:
 *
 * @egg_idletime: This class instance
 **/
static void
egg_idletime_init (EggIdletime *idletime)
{
	int sync_error;
	int ncounters;
	XSyncSystemCounter *counters;
	EggIdletimeAlarm *alarm;
	int i;

	idletime->priv = EGG_IDLETIME_GET_PRIVATE (idletime);

	idletime->priv->array = g_ptr_array_new ();

	idletime->priv->idle_counter = None;
	idletime->priv->last_event = 0;
	idletime->priv->sync_event = 0;
	idletime->priv->dpy = GDK_DISPLAY ();

	/* get the sync event */
	if (!XSyncQueryExtension (idletime->priv->dpy, &idletime->priv->sync_event, &sync_error)) {
		g_warning ("No Sync extension.");
		return;
	}

	/* gtk_init should do XSyncInitialize for us */
	counters = XSyncListSystemCounters (idletime->priv->dpy, &ncounters);
	for (i = 0; i < ncounters && !idletime->priv->idle_counter; i++) {
		if (!strcmp(counters[i].name, "IDLETIME"))
			idletime->priv->idle_counter = counters[i].counter;
	}
//	XSyncFreeSystemCounterList (counters);

	/* arh. we don't have IDLETIME support */
	if (!idletime->priv->idle_counter) {
		g_warning ("No idle counter.");
		return;
	}

	idletime->priv->reset_set = FALSE;

	/* only match on XSyncAlarmNotifyMask */
	gdk_error_trap_push ();
	XSelectInput (idletime->priv->dpy, GDK_ROOT_WINDOW (), XSyncAlarmNotifyMask);
	if (gdk_error_trap_pop ())
		g_warning ("XSelectInput failed");

	/* this is where we want events */
	gdk_window_add_filter (NULL, egg_idletime_x_event_filter, idletime);

	/* create a reset alarm */
	alarm = g_new0 (EggIdletimeAlarm, 1);
	alarm->id = 0;
	g_ptr_array_add (idletime->priv->array, alarm);
}

/**
 * egg_idletime_finalize:
 * @object: This class instance
 **/
static void
egg_idletime_finalize (GObject *object)
{
	guint i;
	EggIdletime *idletime;
	EggIdletimeAlarm *alarm;

	g_return_if_fail (object != NULL);
	g_return_if_fail (EGG_IS_IDLETIME (object));

	idletime = EGG_IDLETIME (object);
	idletime->priv = EGG_IDLETIME_GET_PRIVATE (idletime);

	for (i=0; i<idletime->priv->array->len; i++) {
		alarm = g_ptr_array_index (idletime->priv->array, i);
		egg_idletime_alarm_free (idletime, alarm);
	}
	g_ptr_array_free (idletime->priv->array, TRUE);

	G_OBJECT_CLASS (egg_idletime_parent_class)->finalize (object);
}

/**
 * egg_idletime_new:
 * Return value: new EggIdletime instance.
 **/
EggIdletime *
egg_idletime_new (void)
{
	if (egg_idletime_object != NULL) {
		g_object_ref (egg_idletime_object);
	} else {
		egg_idletime_object = g_object_new (EGG_IDLETIME_TYPE, NULL);
		g_object_add_weak_pointer (egg_idletime_object, &egg_idletime_object);
	}
	return EGG_IDLETIME (egg_idletime_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

static void
egg_test_egg_idletime_wait (guint time_ms)
{
	GTimer *ltimer = g_timer_new ();
	gfloat goal = time_ms / (gfloat) 1000.0f;
	do {
		g_main_context_iteration (NULL, FALSE);
	} while (g_timer_elapsed (ltimer, NULL) < goal);
	g_timer_destroy (ltimer);
}

static guint last_alarm = 0;
static guint event_time;
GTimer *timer;

static void
gpm_alarm_expired_cb (EggIdletime *idletime, guint alarm, gpointer data)
{
	last_alarm = alarm;
	event_time = g_timer_elapsed (timer, NULL) * (gfloat) 1000.0f;
//	g_print ("[evt %i in %ims]\n", alarm, event_time);
}

static void
wait_until_alarm (void)
{
	g_print ("*****************************\n");
	g_print ("*** DO NOT MOVE THE MOUSE ***\n");
	g_print ("*****************************\n");
	while (last_alarm == 0)
		g_main_context_iteration (NULL, FALSE);
}

static void
wait_until_reset (void)
{
	if (last_alarm == 0)
		return;
	g_print ("*****************************\n");
	g_print ("***     MOVE THE MOUSE    ***\n");
	g_print ("*****************************\n");
	while (last_alarm != 0)
		g_main_context_iteration (NULL, FALSE);
	egg_test_egg_idletime_wait (1000);
}

void
egg_idletime_test (gpointer data)
{
	EggIdletime *idletime;
	gboolean ret;
	guint alarm;
	guint i;
	EggTest *test = (EggTest *) data;

	if (egg_test_start (test, "EggIdletime") == FALSE)
		return;

	timer = g_timer_new ();
	gdk_init (NULL, NULL);

	/* warn */

	g_timer_start (timer);
	/************************************************************/
	egg_test_title (test, "check to see if delay works as expected");
	egg_test_egg_idletime_wait (2000);
	event_time = g_timer_elapsed (timer, NULL) * (gfloat) 1000.0f;
	if (event_time > 1800 && event_time < 2200) {
		egg_test_success (test, "time %i~=%i", 2000, event_time);
	} else {
		egg_test_failed (test, "time not the same! %i != %i", event_time, 2000);
	}

	/************************************************************/
	egg_test_title (test, "make sure we get a non null device");
	idletime = egg_idletime_new ();
	if (idletime != NULL) {
		egg_test_success (test, "got EggIdletime");
	} else {
		egg_test_failed (test, "could not get EggIdletime");
	}
	g_signal_connect (idletime, "alarm-expired",
			  G_CALLBACK (gpm_alarm_expired_cb), NULL);

	/************************************************************/
	egg_test_title (test, "check if we are alarm zero with no alarms");
	alarm = egg_idletime_alarm_get (idletime);
	if (alarm == 0) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "alarm %i set!", alarm);
	}

	/************************************************************/
	egg_test_title (test, "check if we can set an reset alarm");
	ret = egg_idletime_alarm_set (idletime, 0, 100);
	if (!ret) {
		egg_test_success (test, "ignored reset alarm");
	} else {
		egg_test_failed (test, "did not ignore reset alarm");
	}

	/************************************************************/
	egg_test_title (test, "check if we can set an alarm timeout of zero");
	ret = egg_idletime_alarm_set (idletime, 999, 0);
	if (!ret) {
		egg_test_success (test, "ignored invalid alarm");
	} else {
		egg_test_failed (test, "did not ignore invalid alarm");
	}

	/************************************************************/
	g_timer_start (timer);
	egg_test_title (test, "check if we can set an alarm");
	ret = egg_idletime_alarm_set (idletime, 101, 5000);
	if (ret) {
		egg_test_success (test, "set alarm okay");
	} else {
		egg_test_failed (test, "could not set alarm");
	}

	egg_idletime_alarm_set (idletime, 101, 5000);
	wait_until_alarm ();

	/* loop this two times */
	for (i=0; i<2; i++) {
		/* just let it time out, and wait for human input */
		wait_until_reset ();
		g_timer_start (timer);

		/************************************************************/
		g_timer_start (timer);
		egg_test_title (test, "check if we can set an alarm");
		ret = egg_idletime_alarm_set (idletime, 101, 5000);
		if (ret) {
			egg_test_success (test, "set alarm 5000ms okay");
		} else {
			egg_test_failed (test, "could not set alarm 5000ms");
		}

		/* wait for alarm to go off */
		wait_until_alarm ();
		g_timer_start (timer);

		/************************************************************/
		egg_test_title (test, "check if correct alarm has gone off");
		alarm = egg_idletime_alarm_get (idletime);
		if (alarm == 101) {
			egg_test_success (test, "correct alarm");
		} else {
			egg_test_failed (test, "alarm %i set!", alarm);
		}

		/************************************************************/
		egg_test_title (test, "check if alarm has gone off in correct time");
		alarm = egg_idletime_alarm_get (idletime);
		if (event_time > 3000 && event_time < 6000) {
			egg_test_success (test, "correct, timeout ideally %ims (we did after %ims)", 5000, event_time);
		} else {
			egg_test_failed (test, "alarm %i did not timeout correctly !", alarm);
		}
	}

	/* just let it time out, and wait for human input */
	wait_until_reset ();
	g_timer_start (timer);

	/************************************************************/
	g_timer_start (timer);
	egg_test_title (test, "check if we can set an existing alarm");
	ret = egg_idletime_alarm_set (idletime, 101, 10000);
	if (ret) {
		egg_test_success (test, "set alarm 10000ms okay");
	} else {
		egg_test_failed (test, "could not set alarm 10000ms");
	}

	/* wait for alarm to go off */
	wait_until_alarm ();
	g_timer_start (timer);

	/************************************************************/
	egg_test_title (test, "check if alarm has gone off in the old time");
	alarm = egg_idletime_alarm_get (idletime);
	if (event_time > 5000) {
		egg_test_success (test, "last timeout value used");
	} else {
		egg_test_failed (test, "incorrect timeout used %ims", event_time);
	}

	/************************************************************/
	egg_test_title (test, "check if we can remove an invalid alarm");
	ret = egg_idletime_alarm_remove (idletime, 202);
	if (!ret) {
		egg_test_success (test, "ignored invalid alarm");
	} else {
		egg_test_failed (test, "removed invalid alarm");
	}

	/************************************************************/
	egg_test_title (test, "check if we can remove an valid alarm");
	ret = egg_idletime_alarm_remove (idletime, 101);
	if (ret) {
		egg_test_success (test, "removed valid alarm");
	} else {
		egg_test_failed (test, "failed to remove valid alarm");
	}

	g_timer_destroy (timer);
	g_object_unref (idletime);

	egg_test_end (test);
}

#endif
