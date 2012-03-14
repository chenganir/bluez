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
#include "log.h"

struct batterystate {
	struct btd_device	*dev;		/* Device reference */
	GAttrib			*attrib;		/* GATT connection */
	guint			attioid;		/* Att watcher id */
	struct att_range	*svc_range;	/* DeviceInfo range */
	GSList			*chars;		/* Characteristics */
};

static GSList *batteryservices = NULL;

struct characteristic {
	struct att_char			attr;	/* Characteristic */
	struct batterystate		*d;		/* deviceinfo where the char belongs */
	GSList			*desc;			/* Descriptors */
};

struct descriptor {
	struct characteristic	*ch;
	uint16_t		handle;
	bt_uuid_t		uuid;
};

static void char_free(gpointer user_data)
{
	struct characteristic *c = user_data;

	g_free(c);
}

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

	if (bs->chars != NULL)
		g_slist_free_full(bs->chars, char_free);

	btd_device_unref(bs->dev);
	g_free(bs->svc_range);
	g_free(bs);
}

static void discover_desc_cb(guint8 status, const guint8 *pdu, guint16 len,
							gpointer user_data)
{
	struct characteristic *ch = user_data;
	struct att_data_list *list;
	uint8_t format;
	int i;

	if (status != 0) {
		error("Discover all characteristic descriptors failed [%s]: %s",
					ch->attr.uuid, att_ecode2str(status));
		return;
	}

	list = dec_find_info_resp(pdu, len, &format);
	if (list == NULL)
		return;

	for (i = 0; i < list->num; i++) {
		struct descriptor *desc;
		uint8_t *value;

		value = list->data[i];
		desc = g_new0(struct descriptor, 1);
		desc->handle = att_get_u16(value);
		desc->ch = ch;

		if (format == 0x01)
			desc->uuid = att_get_uuid16(&value[2]);
		else
			desc->uuid = att_get_uuid128(&value[2]);

		ch->desc = g_slist_append(ch->desc, desc);
	}

	att_data_list_free(list);
}


static void configure_batterystate_cb(GSList *characteristics, guint8 status,
							gpointer user_data)
{
	struct batterystate *bs = user_data;
	GSList *l;

	if (status != 0) {
		error("Discover batterystate characteristics: %s",
							att_ecode2str(status));
		return;
	}

	for (l = characteristics; l; l = l->next) {
		struct att_char *c = l->data;
		struct characteristic *ch;
		uint16_t start, end;

		ch = g_new0(struct characteristic, 1);
		ch->attr.handle = c->handle;
		ch->attr.properties = c->properties;
		ch->attr.value_handle = c->value_handle;
		memcpy(ch->attr.uuid, c->uuid, MAX_LEN_UUID_STR + 1);
		ch->d = bs;

		bs->chars = g_slist_append(bs->chars, ch);

		start = c->value_handle + 1;

		if (l->next != NULL) {
			struct att_char *c = l->next->data;
			if (start == c->handle)
				continue;
			end = c->handle - 1;
		} else if (c->value_handle != bs->svc_range->end)
			end = bs->svc_range->end;
		else
			continue;

		gatt_find_info(bs->attrib, start, end, discover_desc_cb, ch);
	}
}

static void attio_connected_cb(GAttrib *attrib, gpointer user_data)
{
	struct batterystate *bs = user_data;

	bs->attrib = g_attrib_ref(attrib);

	gatt_discover_char(bs->attrib, bs->svc_range->start, bs->svc_range->end,
					NULL, configure_batterystate_cb, bs);

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
	bs->svc_range = g_new0(struct att_range, 1);
	bs->svc_range->start = dattr->start;
	bs->svc_range->end = dattr->end;

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
