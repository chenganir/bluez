/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2012 Texas Instruments, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <bluetooth/uuid.h>

#include "adapter.h"
#include "device.h"
#include "gattrib.h"
#include "attio.h"
#include "att.h"
#include "gatt.h"
#include "batterystate.h"

struct batterystate {
	struct btd_device	*dev;		/* Device reference */
	GAttrib			*attrib;		/* GATT connection */
	guint			attioid;		/* Att watcher id */
};

static GSList *batteryservices = NULL;

static gint cmp_device(gconstpointer a, gconstpointer b)
{
	const struct batterystate *bs = a;
	const struct btd_device *dev = b;

	if (dev == bs->dev)
		return 0;

	return -1;
}

static void batterystate_free(gpointer user_data)
{
	struct batterystate *bs = user_data;

	if (bs->attioid > 0)
		btd_device_remove_attio_callback(bs->dev, bs->attioid);

	if (bs->attrib != NULL)
		g_attrib_unref(bs->attrib);

	btd_device_unref(bs->dev);
	g_free(bs);
}

static void attio_connected_cb(GAttrib *attrib, gpointer user_data)
{
	struct batterystate *bs = user_data;

	bs->attrib = g_attrib_ref(attrib);
}

static void attio_disconnected_cb(gpointer user_data)
{
	struct batterystate *bs = user_data;

	g_attrib_unref(bs->attrib);
	bs->attrib = NULL;
}

int batterystate_register(struct btd_device *device, struct att_primary *dattr)
{
	struct batterystate *bs;

	bs = g_new0(struct batterystate, 1);
	bs->dev = btd_device_ref(device);

	batteryservices = g_slist_prepend(batteryservices, bs);

	bs->attioid = btd_device_add_attio_callback(device, attio_connected_cb,
						attio_disconnected_cb, bs);
	return 0;
}

void batterystate_unregister(struct btd_device *device)
{
	struct batterystate *bs;
	GSList *l;

	l = g_slist_find_custom(batteryservices, device, cmp_device);
	if (l == NULL)
		return;

	bs = l->data;
	batteryservices = g_slist_remove(batteryservices, bs);

	batterystate_free(bs);
}
