/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2005-2007 Richard Hughes <richard@hughsie.com>
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

#ifndef __GPMPOWERMANAGER_H
#define __GPMPOWERMANAGER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GPM_TYPE_POWERMANAGER		(gpm_powermanager_get_type ())
#define GPM_POWERMANAGER(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), GPM_TYPE_POWERMANAGER, GpmPowermanager))
#define GPM_POWERMANAGER_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), GPM_TYPE_POWERMANAGER, GpmPowermanagerClass))
#define GPM_IS_POWERMANAGER(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), GPM_TYPE_POWERMANAGER))
#define GPM_IS_POWERMANAGER_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), GPM_TYPE_POWERMANAGER))
#define GPM_POWERMANAGER_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), GPM_TYPE_POWERMANAGER, GpmPowermanagerClass))

#define	GPM_DBUS_SERVICE		"org.freedesktop.PowerManagement"
#define	GPM_DBUS_INTERFACE		"org.freedesktop.PowerManagement"
#define	GPM_DBUS_INTERFACE_WIDGET	"org.freedesktop.PowerManagement.Widget"
#define	GPM_DBUS_INTERFACE_BACKLIGHT	"org.freedesktop.PowerManagement.Backlight"
#define	GPM_DBUS_INTERFACE_STATS	"org.freedesktop.PowerManagement.Statistics"
#define	GPM_DBUS_INTERFACE_INHIBIT	"org.freedesktop.PowerManagement.Inhibit"
#define	GPM_DBUS_PATH			"/org/freedesktop/PowerManagement"
#define	GPM_DBUS_PATH_BACKLIGHT		"/org/freedesktop/PowerManagement/Backlight"
#define	GPM_DBUS_PATH_WIDGET		"/org/freedesktop/PowerManagement/Widget"
#define	GPM_DBUS_PATH_STATS		"/org/freedesktop/PowerManagement/Statistics"
#define	GPM_DBUS_PATH_INHIBIT		"/org/freedesktop/PowerManagement/Inhibit"

/* common descriptions of this program */
#define GPM_NAME 			_("Power Manager")
#define GPM_DESCRIPTION 		_("Power Manager for the GNOME desktop")

/* help location */
#define GPM_HOMEPAGE_URL	 	"http://www.gnome.org/projects/gnome-power-manager/"
#define GPM_BUGZILLA_URL		"http://bugzilla.gnome.org/buglist.cgi?product=gnome-power-manager"
#define GPM_FAQ_URL			"http://live.gnome.org/GnomePowerManager/Faq"

typedef struct GpmPowermanagerPrivate GpmPowermanagerPrivate;

typedef struct
{
	GObject		       parent;
	GpmPowermanagerPrivate *priv;
} GpmPowermanager;

typedef struct
{
	GObjectClass	parent_class;
} GpmPowermanagerClass;

GType		 gpm_powermanager_get_type		(void);
GpmPowermanager	*gpm_powermanager_new			(void);

gboolean	 gpm_powermanager_get_brightness_lcd	(GpmPowermanager *powermanager,
							 guint		 *brightness);
gboolean	 gpm_powermanager_set_brightness_lcd	(GpmPowermanager *powermanager,
							 guint		  brightness);
gboolean	 gpm_powermanager_inhibit		(GpmPowermanager *powermanager,
							 const gchar     *appname,
		   				    	 const gchar     *reason,
						         guint	         *cookie);
gboolean	 gpm_powermanager_uninhibit		(GpmPowermanager *powermanager,
							 guint            cookie);
gboolean	 gpm_powermanager_has_inhibit		(GpmPowermanager *powermanager,
							 gboolean        *has_inhibit);

G_END_DECLS

#endif	/* __GPMPOWERMANAGER_H */