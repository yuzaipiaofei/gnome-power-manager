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

#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>

#include <libhal-gdevice.h>
#include <libhal-gmanager.h>

#include "gpm-marshal.h"
#include "gpm-ac-adapter.h"
#include "gpm-common.h"
#include "gpm-cell-array.h"
#include "gpm-conf.h"
#include "gpm-cell-unit.h"
#include "gpm-cell.h"
#include "gpm-debug.h"
#include "gpm-warning.h"
#include "gpm-profile.h"

static void     gpm_cell_array_class_init (GpmCellArrayClass *klass);
static void     gpm_cell_array_init       (GpmCellArray      *cell_array);
static void     gpm_cell_array_finalize   (GObject	     *object);

#define GPM_CELL_ARRAY_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPM_TYPE_CELL_ARRAY, GpmCellArrayPrivate))
#define GPM_CELL_ARRAY_TEXT_MIN_ACCURACY	30
#define GPM_CELL_ARRAY_TEXT_MIN_TIME		120
#define GPM_UI_TIME_PRECISION			5*60

struct GpmCellArrayPrivate
{
	HalGManager	*hal_manager;
	GpmCellUnit	 unit;
	GpmAcAdapter	*ac_adapter;
	GpmProfile	*profile;
	GpmConf		*conf;
	GpmWarning	*warning;
	GpmWarningState	 warning_state;
	GPtrArray	*array;
	gboolean	 use_profile_calc;
	gboolean	 done_fully_charged;
	gboolean	 done_recall;
	gboolean	 done_capacity;
};

enum {
	COLLECTION_CHANGED,
	PERCENT_CHANGED,
	FULLY_CHARGED,
	CHARGING_CHANGED,
	DISCHARGING_CHANGED,
	PERHAPS_RECALL,
	CHARGE_LOW,
	CHARGE_CRITICAL,
	CHARGE_ACTION,
	DISCHARGING,
	LOW_CAPACITY,
	LAST_SIGNAL
};

static guint signals [LAST_SIGNAL] = { 0, };

G_DEFINE_TYPE (GpmCellArray, gpm_cell_array, G_TYPE_OBJECT)

/**
 * gpm_cell_get_time_discharge:
 **/
GpmCellUnit *
gpm_cell_array_get_unit (GpmCellArray *cell_array)
{
	GpmCellUnit *unit;

	g_return_val_if_fail (cell_array != NULL, NULL);
	g_return_val_if_fail (GPM_IS_CELL_ARRAY (cell_array), NULL);

	unit = &(cell_array->priv->unit);

	return unit;
}

/**
 * gpm_cell_array_get_num_cells:
 **/
guint
gpm_cell_array_get_num_cells (GpmCellArray *cell_array)
{
	g_return_val_if_fail (cell_array != NULL, 0);
	g_return_val_if_fail (GPM_IS_CELL_ARRAY (cell_array), 0);

	return cell_array->priv->array->len;
}

/**
 * gpm_cell_array_get_icon:
 **/
gchar *
gpm_cell_array_get_icon (GpmCellArray *cell_array)
{
	GpmCellUnit *unit;

	g_return_val_if_fail (cell_array != NULL, 0);
	g_return_val_if_fail (GPM_IS_CELL_ARRAY (cell_array), 0);

	unit = &(cell_array->priv->unit);
	return gpm_cell_unit_get_icon (unit);
}

/**
 * gpm_cell_get_kind:
 **/
GpmCellUnitKind
gpm_cell_array_get_kind (GpmCellArray *cell_array)
{
	GpmCellUnit *unit;

	g_return_val_if_fail (cell_array != NULL, 0);
	g_return_val_if_fail (GPM_IS_CELL_ARRAY (cell_array), 0);

	unit = &(cell_array->priv->unit);
	return unit->kind;
}

/**
 * gpm_cell_get_cell:
 **/
GpmCell *
gpm_cell_array_get_cell (GpmCellArray *cell_array, guint id)
{
	GpmCell *cell;

	g_return_val_if_fail (cell_array != NULL, 0);
	g_return_val_if_fail (GPM_IS_CELL_ARRAY (cell_array), 0);

	if (id > cell_array->priv->array->len) {
		gpm_warning ("not valid cell id");
		return FALSE;
	}

	cell = (GpmCell *) g_ptr_array_index (cell_array->priv->array, id);
	return cell;
}

/**
 * gpm_cell_perhaps_recall_cb:
 */
guint
gpm_cell_array_get_time_until_action (GpmCellArray *cell_array)
{
	GpmCellUnit *unit;
	gboolean use_time_primary;
	guint action_percentage;
	guint action_time;
	gint difference;

	/* clear old values (except previous charge rate) */
	unit = &(cell_array->priv->unit);
	gpm_cell_unit_init (unit);

	/* not valid */
	if (unit->is_charging == TRUE || unit->is_discharging == FALSE) {
		return 0;
	}

	/* only calculate for primary */
	if (unit->kind != GPM_CELL_UNIT_KIND_PRIMARY) {
		return 0;
	}

	/* calculate! */
	gpm_conf_get_bool (cell_array->priv->conf, GPM_CONF_USE_TIME_POLICY, &use_time_primary);
	if (use_time_primary == TRUE) {
		/* simple subtraction */
		gpm_conf_get_uint (cell_array->priv->conf, GPM_CONF_ACTION_TIME, &action_time);
		difference = (gint) unit->time_discharge - (gint) action_time;
	} else {
		/* we have to work out the time for this percentage */
		gpm_conf_get_uint (cell_array->priv->conf, GPM_CONF_ACTION_PERCENTAGE, &action_percentage);
		action_time = gpm_profile_get_time (cell_array->priv->profile, action_percentage, TRUE);
		difference = (gint) unit->time_discharge - (gint) action_time;
	}

	/* if invalid, don't return junk */
	if (difference < 0) {
		gpm_debug ("difference negative, now %i, action %i", unit->time_discharge, action_time);
		return 0;
	}
	return difference;
}

/**
 * gpm_cell_perhaps_recall_cb:
 */
static void
gpm_cell_perhaps_recall_cb (GpmCell *cell, gchar *oem_vendor, gchar *website, GpmCellArray *cell_array)
{
	/* only emit this once per startup */
	if (cell_array->priv->done_recall == FALSE) {
		/* just proxy it to the engine layer */
		gpm_debug ("** EMIT: perhaps-recall");
		g_signal_emit (cell_array, signals [PERHAPS_RECALL], 0, oem_vendor, website);
		cell_array->priv->done_recall = TRUE;
	}
}

/**
 * gpm_cell_low_capacity_cb:
 */
static void
gpm_cell_low_capacity_cb (GpmCell *cell, guint capacity, GpmCellArray *cell_array)
{
	/* only emit this once per startup */
	if (cell_array->priv->done_capacity == FALSE) {
		/* just proxy it to the GUI layer */
		gpm_debug ("** EMIT: low-capacity");
		g_signal_emit (cell_array, signals [LOW_CAPACITY], 0, capacity);
		cell_array->priv->done_capacity = TRUE;
	}
}

/**
 * gpm_cell_array_update:
 *
 * Updates the unit. This is
 * needed on multibattery laptops where the time needs to be computed over
 * two or more battereies. Some laptop batteries discharge one after the other,
 * some discharge simultanously.
 * This also does sanity checking on the values to make sure they are sane.
 */
static gboolean
gpm_cell_array_update (GpmCellArray *cell_array)
{
	GpmCellUnit *unit;
	GpmCellUnit *unit_temp;
	GpmCell *cell;
	guint num_present = 0;
	guint num_discharging = 0;
	guint length;
	guint i;

	/* clear old values (except previous charge rate) */
	unit = &(cell_array->priv->unit);
	gpm_cell_unit_init (unit);

	length = cell_array->priv->array->len;

	/* if we have no devices, don't try to average */
	if (length == 0) {
		gpm_debug ("no devices of type %i", unit->kind);
		return FALSE;
	}

	/* iterate thru all the devices to handle multiple batteries */
	for (i=0;i<length;i++) {

		/* get the correct cell */
		cell = gpm_cell_array_get_cell (cell_array, i);
		unit_temp = gpm_cell_get_unit (cell);

		if (unit_temp->is_present == FALSE) {
			gpm_debug ("ignoring device that is not present");
			continue;
		}

		num_present++;

		/* Only one device has to be present for the class to
		 * be present. */
		unit->is_present = TRUE;

		if (unit_temp->is_charging == TRUE) {
			unit->is_charging = TRUE;
		}

		if (unit_temp->is_discharging == TRUE) {
			unit->is_discharging = TRUE;
			num_discharging++;
		}

		unit->percentage += unit_temp->percentage;
		unit->charge_design += unit_temp->charge_design;
		unit->charge_last_full += unit_temp->charge_last_full;
		unit->charge_current += unit_temp->charge_current;
		unit->rate += unit_temp->rate;
		unit->voltage += unit_temp->voltage;
		/* we have to sum this here, in case the device has no rate
		   data, and we can't compute it further down */
		unit->time_charge += unit_temp->time_charge;
		unit->time_discharge += unit_temp->time_discharge;
	}

	/* if we have no present devices, don't try to average */
	if (unit->is_present == FALSE) {
		return FALSE;
	}

	/* average out some values for the global device */
	if (num_present > 1) {
		unit->percentage /= num_present;
		unit->charge_design /= num_present;
		unit->charge_last_full /= num_present;
		unit->charge_current /= num_present;
		unit->rate /= num_present;
		unit->voltage /= num_present;
		unit->time_charge /= num_present;
		unit->time_discharge /= num_present;
	}

	/* sanity check */
	if (unit->is_discharging == TRUE && unit->is_charging == TRUE) {
		gpm_warning ("Sanity check kicked in! "
			     "Multiple device object cannot be charging and discharging simultaneously!");
		unit->is_charging = FALSE;
	}

	gpm_debug ("%i devices of type %s", num_present, gpm_cell_unit_get_kind_string (unit));

	/* Perform following calculations with floating point otherwise we might
	 * get an with batteries which have a very small charge unit and consequently
	 * a very high charge. Fixes bug #327471 */
	if (unit->kind != GPM_CELL_UNIT_KIND_PRIMARY) {
		gint pc = 100 * ((gfloat)unit->charge_current /
				(gfloat)unit->charge_last_full);
		if (pc < 0) {
			gpm_warning ("Corrected percentage charge (%i) and set to minimum", pc);
			pc = 0;
		} else if (pc > 100) {
			gpm_warning ("Corrected percentage charge (%i) and set to maximum", pc);
			pc = 100;
		}
		unit->percentage = pc;
	}

	/* If the primary battery is neither charging nor discharging, and
	 * the charge is low the battery is most likely broken.
	 * In this case, we'll use the ac_adaptor to determine whether it's
	 * charging or not. */
	if (unit->kind == GPM_CELL_UNIT_KIND_PRIMARY &&
	    unit->is_charging == FALSE &&
	    unit->is_discharging == FALSE &&
	    unit->percentage > 0 &&
	    unit->percentage < GPM_CELL_UNIT_MIN_CHARGED_PERCENTAGE) {
		gboolean on_ac;

		/* get the ac state */
		on_ac = gpm_ac_adapter_is_present (cell_array->priv->ac_adapter);
		gpm_debug ("Battery is neither charging nor discharging, "
			   "using ac_adaptor value %i", on_ac);
		if (on_ac == TRUE) {
			unit->is_charging = TRUE;
			unit->is_discharging = FALSE;
		} else {
			unit->is_charging = FALSE;
			unit->is_discharging = TRUE;
		}
	}

	/* We may want to use the old time remaining code.
	 * Hopefully we can remove this in 2.19.x sometime. */
	if (cell_array->priv->use_profile_calc == TRUE &&
	    unit->kind == GPM_CELL_UNIT_KIND_PRIMARY) {
		gpm_debug ("unit->percentage = %i", unit->percentage);
		unit->time_discharge = gpm_profile_get_time (cell_array->priv->profile, unit->percentage, TRUE);
		unit->time_charge = gpm_profile_get_time (cell_array->priv->profile, unit->percentage, FALSE);
	} else {
		/* We only do the "better" remaining time algorithm if the battery has rate,
		 * i.e not a UPS, which gives it's own battery.time_charge but has no rate */
		if (unit->rate > 0) {
			if (unit->is_discharging == TRUE) {
				unit->time_discharge = 3600 * ((float)unit->charge_current /
								      (float)unit->rate);
			} else if (unit->is_charging == TRUE) {
				unit->time_charge = 3600 *
					((float)(unit->charge_last_full - unit->charge_current) /
					(float)unit->rate);
			}
		}
		/* Check the remaining time is under a set limit, to deal with broken
		   primary batteries rate. Fixes bug #328927 */
		if (unit->time_charge > (100 * 60 * 60)) {
			gpm_warning ("Another sanity check kicked in! "
				     "Remaining time cannot be > 100 hours!");
			unit->time_charge = 0;
		}
		if (unit->time_discharge > (100 * 60 * 60)) {
			gpm_warning ("Another sanity check kicked in! "
				     "Remaining time cannot be > 100 hours!");
			unit->time_discharge = 0;
		}
	}
	return TRUE;
}

/**
 * gpm_cell_array_get_config_id:
 *
 * Gets an ID that represents the battery state of the system, typically
 * which will consist of all the serial numbers of primary batteries in the
 * system joined together.
 */
static gchar *
gpm_cell_array_get_config_id (GpmCellArray *cell_array)
{
	GpmCellUnit *unit;
	GpmCell *cell;
	gchar *id;
	gchar *array_id = NULL;
	gchar *new;
	guint length;
	guint i;

	unit = &(cell_array->priv->unit);

	/* invalid if not primary */
	if (unit->kind != GPM_CELL_UNIT_KIND_PRIMARY) {
		gpm_warning ("only valid for primary");
		return NULL;
	}

	length = cell_array->priv->array->len;
	/* if we have no devices, don't try to get id */
	if (length == 0) {
		gpm_debug ("no devices of type primary");
		return NULL;
	}

	/* iterate thru all the devices */
	for (i=0;i<length;i++) {
		/* get the correct cell */
		cell = gpm_cell_array_get_cell (cell_array, i);
		id = gpm_cell_get_id (cell);
		if (array_id == NULL) {
			array_id = id;
		} else {
			new = g_strjoin ("-", array_id, id, NULL);
			g_free (array_id);
			g_free (id);
			array_id = new;
		}
	}
	return array_id;
}

/**
 * gpm_cell_array_percent_changed:
 *
 * Do the clever actions here.
 *
 */
static void
gpm_cell_array_percent_changed (GpmCellArray *cell_array)
{
	GpmWarningState warning_state;
	GpmCellUnit *unit;

	unit = &(cell_array->priv->unit);

	gpm_debug ("printing combined new device:");
	gpm_cell_unit_print (unit);

	/* only emit if all devices are fully charged */
	if (cell_array->priv->done_fully_charged == FALSE &&
	    gpm_cell_unit_is_charged (unit) == TRUE) {
		gpm_debug ("** EMIT: fully-charged");
		g_signal_emit (cell_array, signals [FULLY_CHARGED], 0);
		cell_array->priv->done_fully_charged = TRUE;
	}

	/* We only re-enable the fully charged notification when the battery
	   drops down to 95% as some batteries charge to 100% and then fluctuate
	   from ~98% to 100%. See #338281 for details */
	if (cell_array->priv->done_fully_charged == TRUE &&
	    unit->percentage < GPM_CELL_UNIT_MIN_CHARGED_PERCENTAGE &&
	    gpm_cell_unit_is_charged (unit) == FALSE) {
		gpm_debug ("enabled fully charged");
		cell_array->priv->done_fully_charged = FALSE;
	}

	/* only get a warning state if we are discharging */
	if (unit->is_discharging == TRUE) {
		warning_state = gpm_warning_get_state (cell_array->priv->warning, unit);
	} else {
		warning_state = GPM_WARNING_NONE;
	}

	/* see if we need to issue a warning */
	if (warning_state == GPM_WARNING_NONE) {
		gpm_debug ("No warning");
	} else {
		/* Always check if we already notified the user */
		if (warning_state <= cell_array->priv->warning_state) {
			gpm_debug ("Already notified %i", warning_state);
		} else {

			/* As the level is more critical than the last warning, save it */
			cell_array->priv->warning_state = warning_state;

			gpm_debug ("warning state = %i", warning_state);
			if (warning_state == GPM_WARNING_ACTION) {
				gpm_debug ("** EMIT: charge-action");
				g_signal_emit (cell_array, signals [CHARGE_ACTION], 0, unit->percentage);
			} else if (warning_state == GPM_WARNING_CRITICAL) {
				gpm_debug ("** EMIT: charge-critical");
				g_signal_emit (cell_array, signals [CHARGE_CRITICAL], 0, unit->percentage);
			} else if (warning_state == GPM_WARNING_LOW) {
				gpm_debug ("** EMIT: charge-low");
				g_signal_emit (cell_array, signals [CHARGE_LOW], 0, unit->percentage);
			} else if (warning_state == GPM_WARNING_DISCHARGING) {
				gpm_debug ("** EMIT: discharging");
				g_signal_emit (cell_array, signals [DISCHARGING], 0, unit->percentage);
			}
		}
	}
}

/**
 * gpm_cell_percent_changed_cb:
 */
static void
gpm_cell_percent_changed_cb (GpmCell *cell, guint percent, GpmCellArray *cell_array)
{
	GpmCellUnit *unit;
	guint old_percent;

	unit = &(cell_array->priv->unit);

	/* save the old percentage so we can compare it later */
	old_percent = unit->percentage;

	/* recalculate */
	gpm_cell_array_update (cell_array);

	/* provide data if we are primary. Will need profile if multibattery */
	if (unit->kind == GPM_CELL_UNIT_KIND_PRIMARY) {
		gpm_profile_register_percentage (cell_array->priv->profile, percent);
	}

	/* proxy to engine if different */
	if (old_percent != unit->percentage) {
		gpm_debug ("** EMIT: percent-changed");
		g_signal_emit (cell_array, signals [PERCENT_CHANGED], 0, unit->percentage);
		gpm_cell_array_percent_changed (cell_array);
	}
}

/**
 * gpm_cell_charging_changed_cb:
 */
static void
gpm_cell_charging_changed_cb (GpmCell *cell, gboolean charging, GpmCellArray *cell_array)
{
	GpmCellUnit *unit;

	unit = &(cell_array->priv->unit);

	/* recalculate */
	gpm_cell_array_update (cell_array);

	gpm_debug ("printing combined new device:");
	gpm_cell_unit_print (unit);

	/* invalidate warning */
	if (unit->is_discharging == FALSE || unit->is_charging == TRUE) {
		gpm_debug ("warning state invalidated");
		cell_array->priv->warning_state = GPM_WARNING_NONE;
	}

	/* provide data if we are primary. */
	if (unit->kind == GPM_CELL_UNIT_KIND_PRIMARY) {
		gpm_profile_register_charging (cell_array->priv->profile, charging);
	}

	/* proxy to engine */
	gpm_debug ("** EMIT: charging-changed");
	g_signal_emit (cell_array, signals [CHARGING_CHANGED], 0, charging);
}

/**
 * gpm_cell_discharging_changed_cb:
 */
static void
gpm_cell_discharging_changed_cb (GpmCell *cell, gboolean discharging, GpmCellArray *cell_array)
{
	GpmCellUnit *unit;

	unit = &(cell_array->priv->unit);

	/* recalculate */
	gpm_cell_array_update (cell_array);

	gpm_debug ("printing combined new device:");
	gpm_cell_unit_print (unit);

	/* invalidate warning */
	if (unit->is_discharging == FALSE || unit->is_charging == TRUE) {
		cell_array->priv->warning_state = GPM_WARNING_NONE;
	}

	/* proxy to engine */
	gpm_debug ("** EMIT: discharging-changed");
	g_signal_emit (cell_array, signals [DISCHARGING_CHANGED], 0, discharging);
}

/**
 * gpm_cell_array_index_udi:
 *
 * Returns -1 if not found
 */
static gint
gpm_cell_array_index_udi (GpmCellArray *cell_array, const gchar *udi)
{
	gint i;
	guint length;
	GpmCell *cell;

	length = cell_array->priv->array->len;
	for (i=0;i<length;i++) {
		cell = (GpmCell *) g_ptr_array_index (cell_array->priv->array, i);
		if (strcmp (gpm_cell_get_udi (cell), udi) == 0) {
			gpm_debug ("Found %s with udi check", udi);
			return i;
		}
	}
	/* did not find */
	return -1;
}

/**
 * gpm_check_device_key:
 **/
static gboolean
gpm_check_device_key (GpmCellArray *cell_array, const gchar *udi, const gchar *key, const gchar *value)
{
	HalGDevice *device;
	gboolean ret;
	gboolean matches = FALSE;
	gchar *rettype;

	device = hal_gdevice_new ();
	ret = hal_gdevice_set_udi (device, udi);
	if (ret == FALSE) {
		gpm_warning ("failed to set UDI %s", udi);
		return FALSE;
	}

	/* check type */
	ret = hal_gdevice_get_string (device, key, &rettype, NULL);
	if (ret == FALSE || rettype == NULL) {
		gpm_warning ("failed to get %s", key);
		return FALSE;
	}
	gpm_debug ("checking %s against %s", rettype, value);
	if (strcmp (rettype, value) == 0) {
		matches = TRUE;
	}
	g_free (rettype);
	g_object_unref (device);
	return matches;
}

/**
 * gpm_cell_array_collection_changed:
 *
 * i.e. a battery was inserted or removed
 */
static gboolean
gpm_cell_array_collection_changed (GpmCellArray *cell_array)
{
	GpmCellUnit *unit;
	gchar *config_id;

	unit = &(cell_array->priv->unit);

	/* reset the profile config id if primary */
	if (unit->kind == GPM_CELL_UNIT_KIND_PRIMARY) {
		/* we have to use a profile ID for hot-swapping batteries */
		config_id = gpm_cell_array_get_config_id (cell_array);
		gpm_profile_set_config_id (cell_array->priv->profile, config_id);
		g_free (config_id);
	}

	/* recalculate */
	gpm_cell_array_update (cell_array);

	/* emit */
	gpm_debug ("** EMIT: collection-changed");
	g_signal_emit (cell_array, signals [COLLECTION_CHANGED], 0, unit->percentage);

	/* make sure */
	gpm_cell_array_percent_changed (cell_array);
	return TRUE;
}

/**
 * gpm_cell_array_add:
 */
static gboolean
gpm_cell_array_add (GpmCellArray *cell_array, const gchar *udi)
{
	GpmCell *cell;
	GpmCellUnit *unit;
	gint index;
	const gchar *kind_string;
	gboolean ret;

	unit = &(cell_array->priv->unit);

	/* check type */
	kind_string = gpm_cell_unit_get_kind_string (unit);
	ret = gpm_check_device_key (cell_array, udi, "battery.type", kind_string);
	if (ret == FALSE) {
		gpm_debug ("not adding %s for %s", udi, kind_string);
		return FALSE;
	}

	/* is this UDI in our array? */
	index = gpm_cell_array_index_udi (cell_array, udi);
	if (index != -1) {
		/* already added */
		gpm_debug ("already added %s", udi);
		return FALSE;
	}

	gpm_debug ("adding the right kind of battery: %s", kind_string);

	cell = gpm_cell_new ();
	g_signal_connect (cell, "perhaps-recall",
			  G_CALLBACK (gpm_cell_perhaps_recall_cb), cell_array);
	g_signal_connect (cell, "low-capacity",
			  G_CALLBACK (gpm_cell_low_capacity_cb), cell_array);
	g_signal_connect (cell, "percent-changed",
			  G_CALLBACK (gpm_cell_percent_changed_cb), cell_array);
	g_signal_connect (cell, "charging-changed",
			  G_CALLBACK (gpm_cell_charging_changed_cb), cell_array);
	g_signal_connect (cell, "discharging-changed",
			  G_CALLBACK (gpm_cell_discharging_changed_cb), cell_array);
	gpm_cell_set_type (cell, unit->kind, udi);
	gpm_cell_print (cell);

	g_ptr_array_add (cell_array->priv->array, (gpointer) cell);

	/* global collection has changed */
	gpm_cell_array_collection_changed (cell_array);

	return TRUE;
}

/**
 * gpm_cell_array_coldplug:
 **/
static gboolean
gpm_cell_array_coldplug (GpmCellArray *cell_array)
{
	guint i;
	GError *error;
	gchar **device_names = NULL;
	gboolean ret;

	/* get all the hal devices of this type */
	error = NULL;
	ret = hal_gmanager_find_capability (cell_array->priv->hal_manager, "battery", &device_names, &error);
	if (ret == FALSE) {
		gpm_warning ("Couldn't obtain list of batteries: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* Try to add all, the add will fail for batteries not of the correct type */
	for (i=0; device_names[i]; i++) {
		gpm_cell_array_add (cell_array, device_names[i]);
	}

	hal_gmanager_free_capability (device_names);
	return TRUE;
}

/**
 * gpm_cell_array_set_type:
 **/
gboolean
gpm_cell_array_set_type (GpmCellArray *cell_array, GpmCellUnitKind kind)
{
	GpmCellUnit *unit;

	g_return_val_if_fail (cell_array != NULL, FALSE);
	g_return_val_if_fail (GPM_IS_CELL_ARRAY (cell_array), FALSE);

	unit = &(cell_array->priv->unit);

	/* get the correct type */
	unit->kind = kind;

	/* coldplug */
	gpm_cell_array_coldplug (cell_array);

	/* recalculate */
	gpm_cell_array_update (cell_array);

	return TRUE;
}

/**
 * gpm_cell_array_free:
 **/
static void
gpm_cell_array_free (GpmCellArray *cell_array)
{
	GpmCell *cell;
	guint length;
	guint i;

	length = cell_array->priv->array->len;
	/* iterate thru all the devices to free */
	for (i=0;i<length;i++) {
		/* get the correct cell */
		cell = gpm_cell_array_get_cell (cell_array, i);
		g_object_unref (cell);
	}

	/* recreate array */
	g_ptr_array_free (cell_array->priv->array, TRUE);
	cell_array->priv->array = g_ptr_array_new ();
}

/**
 * gpm_cell_array_refresh:
 **/
gboolean
gpm_cell_array_refresh (GpmCellArray *cell_array)
{
	g_return_val_if_fail (cell_array != NULL, FALSE);
	g_return_val_if_fail (GPM_IS_CELL_ARRAY (cell_array), FALSE);

	/* free all, then re-coldplug */
	gpm_cell_array_free (cell_array);
	gpm_cell_array_coldplug (cell_array);
	return TRUE;
}

/**
 * gpm_cell_array_get_description:
 **/
gchar *
gpm_cell_array_get_description (GpmCellArray *cell_array)
{
	const gchar *type_desc = NULL;
	gchar *charge_timestring;
	gchar *discharge_timestring;
	gchar *description = NULL;
	guint accuracy;
	guint time;
	guint charge_time_round;
	guint discharge_time_round;
	GpmCellUnit *unit;
	gboolean plural = FALSE;

	g_return_val_if_fail (cell_array != NULL, 0);
	g_return_val_if_fail (GPM_IS_CELL_ARRAY (cell_array), 0);

	unit = &(cell_array->priv->unit);

	if (unit->is_present == FALSE) {
		return NULL;
	}

	/* localized name */
	if (cell_array->priv->array->len > 1) {
		plural = TRUE;
	}
	type_desc = gpm_cell_unit_get_kind_localised (unit, plural);

	/* don't display all the extra stuff for keyboards and mice */
	if (unit->kind == GPM_CELL_UNIT_KIND_MOUSE ||
	    unit->kind == GPM_CELL_UNIT_KIND_KEYBOARD ||
	    unit->kind == GPM_CELL_UNIT_KIND_PDA) {
		return g_strdup_printf ("%s (%i%%)\n", type_desc, unit->percentage);
	}

	/* don't display the text if we are low in accuracy */
	accuracy = gpm_profile_get_accuracy (cell_array->priv->profile, unit->percentage);
	gpm_debug ("accuracy = %i", accuracy);

	/* precalculate so we don't get Unknown time remaining */
	charge_time_round = gpm_precision_round_down (unit->time_charge, GPM_UI_TIME_PRECISION);
	discharge_time_round = gpm_precision_round_down (unit->time_discharge, GPM_UI_TIME_PRECISION);

	/* We always display "Laptop battery 16 minutes remaining" as we need
	   to clarify what device we are refering to. For details see :
	   http://bugzilla.gnome.org/show_bug.cgi?id=329027 */
	if (gpm_cell_unit_is_charged (unit) == TRUE) {

		if (unit->kind == GPM_CELL_UNIT_KIND_PRIMARY &&
		    accuracy > GPM_CELL_ARRAY_TEXT_MIN_ACCURACY) {
			time = gpm_profile_get_time (cell_array->priv->profile, unit->percentage, TRUE);
			discharge_time_round = gpm_precision_round_down (time, GPM_UI_TIME_PRECISION);
			discharge_timestring = gpm_get_timestring (discharge_time_round);
			description = g_strdup_printf (_("%s fully charged (%i%%)\nProvides %s battery runtime\n"),
							type_desc, unit->percentage, discharge_timestring);
			g_free (discharge_timestring);
		} else {
			description = g_strdup_printf (_("%s fully charged (%i%%)\n"),
							type_desc, unit->percentage);
		}

	} else if (unit->is_discharging == TRUE) {

		if (discharge_time_round > GPM_CELL_ARRAY_TEXT_MIN_TIME) {
			discharge_timestring = gpm_get_timestring (discharge_time_round);
			description = g_strdup_printf (_("%s %s remaining (%i%%)\n"),
						type_desc, discharge_timestring, unit->percentage);
			g_free (discharge_timestring);
		} else {
			/* don't display "Unknown remaining" */
			description = g_strdup_printf (_("%s discharging (%i%%)\n"),
						type_desc, unit->percentage);
		}

	} else if (unit->is_charging == TRUE) {

		if (charge_time_round > GPM_CELL_ARRAY_TEXT_MIN_TIME &&
		    discharge_time_round > GPM_CELL_ARRAY_TEXT_MIN_TIME &&
		    accuracy > GPM_CELL_ARRAY_TEXT_MIN_ACCURACY) {
			/* display both discharge and charge time */
			charge_timestring = gpm_get_timestring (charge_time_round);
			discharge_timestring = gpm_get_timestring (discharge_time_round);
			description = g_strdup_printf (_("%s %s until charged (%i%%)\nProvides %s battery runtime\n"),
							type_desc, charge_timestring, unit->percentage, discharge_timestring);
			g_free (charge_timestring);
			g_free (discharge_timestring);
		} else if (charge_time_round > GPM_CELL_ARRAY_TEXT_MIN_TIME) {
			/* display only charge time */
			charge_timestring = gpm_get_timestring (charge_time_round);
			description = g_strdup_printf (_("%s %s until charged (%i%%)\n"),
						type_desc, charge_timestring, unit->percentage);
			g_free (charge_timestring);
		} else {
			/* don't display "Unknown remaining" */
			description = g_strdup_printf (_("%s charging (%i%%)\n"),
						type_desc, unit->percentage);
		}

	} else {
		gpm_warning ("in an undefined state we are not charging or "
			     "discharging and the batteries are also not charged");
	}
	return description;
}

/**
 * hal_device_removed_cb:
 *
 * @hal: The hal instance
 * @udi: The HAL UDI
 * @cell_array: This cell_array instance
 */
static gboolean
hal_device_removed_cb (HalGManager  *hal_manager,
		       const gchar  *udi,
		       GpmCellArray *cell_array)
{
	gint index;
	GpmCell *cell;

	/* is this UDI in our array? */
	index = gpm_cell_array_index_udi (cell_array, udi);
	if (index == -1) {
		/* nope */
		return FALSE;
	}

	gpm_debug ("Removing udi=%s", udi);

	/* we unref as the device has gone away */
	cell = (GpmCell *) g_ptr_array_index (cell_array->priv->array, index);
	g_object_unref (cell);

	/* remove from the devicestore */
	g_ptr_array_remove_index (cell_array->priv->array, index);

	/* global collection has changed */
	gpm_cell_array_collection_changed (cell_array);

	return TRUE;
}

/**
 * hal_new_capability_cb:
 *
 * @hal: The hal instance
 * @udi: The HAL UDI
 * @capability: the capability, e.g. "battery"
 * @cell_array: This cell_array instance
 */
static void
hal_new_capability_cb (HalGManager  *hal_manager,
		       const gchar  *udi,
		       const gchar  *capability,
		       GpmCellArray *cell_array)
{
	gpm_debug ("udi=%s, capability=%s", udi, capability);

	if (strcmp (capability, "battery") == 0) {
		gpm_cell_array_add (cell_array, udi);
	}
}

/**
 * hal_device_added_cb:
 *
 * @hal: The hal instance
 * @udi: The HAL UDI
 * @cell_array: This cell_array instance
 */
static void
hal_device_added_cb (HalGManager  *hal_manager,
		     const gchar  *udi,
		     GpmCellArray *cell_array)
{
	gboolean is_battery;
	gboolean dummy;
	HalGDevice *device;

	/* find out if the new device has capability battery
	   this might fail for CSR as the addon is weird */
	device = hal_gdevice_new ();
	hal_gdevice_set_udi (device, udi);
	hal_gdevice_query_capability (device, "battery", &is_battery, NULL);

	/* try harder */
	if (is_battery == FALSE) {
		is_battery = hal_gdevice_get_bool (device, "battery.present", &dummy, NULL);
	}

	/* if a battery, then add */
	if (is_battery == TRUE) {
		gpm_cell_array_add (cell_array, udi);
	}
	g_object_unref (device);
}

/**
 * conf_key_changed_cb:
 **/
static void
conf_key_changed_cb (GpmConf      *conf,
		     const gchar  *key,
		     GpmCellArray *cell_array)
{
	GpmCellUnit *unit;

	if (strcmp (key, GPM_CONF_USE_PROFILE_TIME) == 0) {
		gpm_conf_get_bool (conf, GPM_CONF_USE_PROFILE_TIME, &cell_array->priv->use_profile_calc);
		/* recalculate */
		gpm_cell_array_update (cell_array);

		/* emit signal to get everything up from here to update */
		gpm_debug ("** EMIT: percent-changed");
		unit = &(cell_array->priv->unit);
		g_signal_emit (cell_array, signals [PERCENT_CHANGED], 0, unit->percentage);
	}
}

/**
 * hal_daemon_start_cb:
 **/
static void
hal_daemon_start_cb (HalGManager  *hal_manager,
		     GpmCellArray *cell_array)
{
	/* coldplug all batteries back again */
	gpm_cell_array_coldplug (cell_array);
}

/**
 * hal_daemon_stop_cb:
 **/
static void
hal_daemon_stop_cb (HalGManager  *hal_manager,
		    GpmCellArray *cell_array)
{
	/* remove old devices */
	gpm_cell_array_free (cell_array);
}

/**
 * gpm_cell_array_class_init:
 * @cell_array: This class instance
 **/
static void
gpm_cell_array_class_init (GpmCellArrayClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpm_cell_array_finalize;
	g_type_class_add_private (klass, sizeof (GpmCellArrayPrivate));

	signals [PERCENT_CHANGED] =
		g_signal_new ("percent-changed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpmCellArrayClass, percent_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [CHARGING_CHANGED] =
		g_signal_new ("charging-changed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpmCellArrayClass, charging_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals [DISCHARGING_CHANGED] =
		g_signal_new ("discharging-changed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpmCellArrayClass, discharging_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	signals [LOW_CAPACITY] =
		g_signal_new ("low-capacity",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpmCellArrayClass, low_capacity),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [PERHAPS_RECALL] =
		g_signal_new ("perhaps-recall",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpmCellArrayClass, perhaps_recall),
			      NULL, NULL,
			      gpm_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE,
			      2, G_TYPE_STRING, G_TYPE_STRING);
	signals [FULLY_CHARGED] =
		g_signal_new ("fully-charged",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpmCellArrayClass, fully_charged),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [COLLECTION_CHANGED] =
		g_signal_new ("collection-changed",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpmCellArrayClass, collection_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [DISCHARGING] =
		g_signal_new ("discharging",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpmCellArrayClass, discharging),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	signals [CHARGE_ACTION] =
		g_signal_new ("charge-action",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpmCellArrayClass, charge_action),
			      NULL, NULL,
			      gpm_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [CHARGE_LOW] =
		g_signal_new ("charge-low",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpmCellArrayClass, charge_low),
			      NULL, NULL,
			      gpm_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	signals [CHARGE_CRITICAL] =
		g_signal_new ("charge-critical",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpmCellArrayClass, charge_critical),
			      NULL, NULL,
			      gpm_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
}

/**
 * gpm_cell_array_init:
 * @cell_array: This class instance
 **/
static void
gpm_cell_array_init (GpmCellArray *cell_array)
{
	cell_array->priv = GPM_CELL_ARRAY_GET_PRIVATE (cell_array);

	cell_array->priv->array = g_ptr_array_new ();
	cell_array->priv->profile = gpm_profile_new ();
	cell_array->priv->conf = gpm_conf_new ();
	g_signal_connect (cell_array->priv->conf, "value-changed",
			  G_CALLBACK (conf_key_changed_cb), cell_array);
	gpm_conf_get_bool (cell_array->priv->conf, GPM_CONF_USE_PROFILE_TIME, &cell_array->priv->use_profile_calc);

	cell_array->priv->done_recall = FALSE;
	cell_array->priv->done_capacity = FALSE;

	/* we don't notify on initial startup */
	cell_array->priv->done_fully_charged = TRUE;

	cell_array->priv->ac_adapter = gpm_ac_adapter_new ();
	cell_array->priv->warning = gpm_warning_new ();
	cell_array->priv->hal_manager = hal_gmanager_new ();
	g_signal_connect (cell_array->priv->hal_manager, "device-added",
			  G_CALLBACK (hal_device_added_cb), cell_array);
	g_signal_connect (cell_array->priv->hal_manager, "device-removed",
			  G_CALLBACK (hal_device_removed_cb), cell_array);
	g_signal_connect (cell_array->priv->hal_manager, "new-capability",
			  G_CALLBACK (hal_new_capability_cb), cell_array);
	g_signal_connect (cell_array->priv->hal_manager, "daemon-start",
			  G_CALLBACK (hal_daemon_start_cb), cell_array);
	g_signal_connect (cell_array->priv->hal_manager, "daemon-stop",
			  G_CALLBACK (hal_daemon_stop_cb), cell_array);

	gpm_cell_unit_init (&cell_array->priv->unit);
}

/**
 * gpm_cell_array_finalize:
 * @object: This class instance
 **/
static void
gpm_cell_array_finalize (GObject *object)
{
	GpmCellArray *cell_array;

	g_return_if_fail (object != NULL);
	g_return_if_fail (GPM_IS_CELL_ARRAY (object));

	cell_array = GPM_CELL_ARRAY (object);
	cell_array->priv = GPM_CELL_ARRAY_GET_PRIVATE (cell_array);

	gpm_cell_array_free (cell_array);
	g_ptr_array_free (cell_array->priv->array, TRUE);

	g_object_unref (cell_array->priv->ac_adapter);
	g_object_unref (cell_array->priv->warning);
	g_object_unref (cell_array->priv->hal_manager);
	g_object_unref (cell_array->priv->profile);
	g_object_unref (cell_array->priv->conf);
}

/**
 * gpm_cell_array_new:
 * Return value: new class instance.
 **/
GpmCellArray *
gpm_cell_array_new (void)
{
	GpmCellArray *cell_array;
	cell_array = g_object_new (GPM_TYPE_CELL_ARRAY, NULL);
	return GPM_CELL_ARRAY (cell_array);
}
