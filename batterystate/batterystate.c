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
#include "att.h"
#include "batterystate.h"

struct batterystate {
	struct btd_device	*dev;		/* Device reference */
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

	btd_device_unref(bs->dev);
	g_free(bs);
}


int batterystate_register(struct btd_device *device, struct att_primary *dattr)
{
	struct batterystate *bs;

	bs = g_new0(struct batterystate, 1);
	bs->dev = btd_device_ref(device);

	batteryservices = g_slist_prepend(batteryservices, bs);

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
