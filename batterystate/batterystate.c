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

#include <gdbus.h>
#include <errno.h>
#include <dbus/dbus.h>
#include <bluetooth/uuid.h>

#include "adapter.h"
#include "device.h"
#include "gattrib.h"
#include "attio.h"
#include "att.h"
#include "gatt.h"
#include "dbus-common.h"
#include "batterystate.h"
#include "log.h"

#define BATTERY_INTERFACE "org.bluez.Battery"

#define BATTERY_LEVEL_UUID		                "00002a19-0000-1000-8000-00805f9b34fb"

struct batterystate {
	DBusConnection		*conn;		/* The connection to the bus */
	char				*path;		/* object path */
	struct btd_device	*dev;		/* Device reference */
	GAttrib			*attrib;		/* GATT connection */
	guint			attioid;		/* Att watcher id */
	struct att_range	*svc_range;	/* Battery Service range */
	GSList			*chars;		/* Characteristics */
	uint8_t 		ns;				/* Battery Namespace */
	uint16_t		description;	/* Battery desciption */
	uint8_t			level;			/* Battery last known level */
};

static GSList *batteryservices = NULL;

struct characteristic {
	struct att_char			attr;	/* Characteristic */
	struct batterystate		*bs;		/* batterystate where the char belongs */
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

	if (bs->path != NULL)
		g_free(bs->path);

	btd_device_unref(bs->dev);
	g_free(bs->svc_range);
	g_free(bs);
}

static void read_batterylevel_cb(guint8 status, const guint8 *pdu, guint16 len,
							gpointer user_data)
{
	struct characteristic *ch = user_data;
	uint8_t value[ATT_MAX_MTU];
	int vlen;

	if (status != 0) {
		error("value read failed: %s",
							att_ecode2str(status));
		return;
	}

	if (!dec_read_resp(pdu, len, value, &vlen)) {
		error("Protocol error\n");
		return;
	}

	if (vlen < 1) {
		DBG("Invalid batterylevel received");
		return;
	}

	ch->bs->level = value[0];
}

static void process_batteryservice_char(struct characteristic *ch)
{
	if (g_strcmp0(ch->attr.uuid, BATTERY_LEVEL_UUID) == 0) {
		gatt_read_char(ch->bs->attrib, ch->attr.value_handle, 0,
							read_batterylevel_cb, ch);
		return;
	}
}

static void batterylevel_presentation_format_desc_cb(guint8 status, const guint8 *pdu, guint16 len,
							gpointer user_data)
{
	struct descriptor *desc = user_data;
	uint8_t value[ATT_MAX_MTU];
	int vlen;

	if (status != 0) {
		DBG("Presentation Format descriptor read failed: %s",
							att_ecode2str(status));
		return;
	}

	if (!dec_read_resp(pdu, len, value, &vlen)) {
		error("Protocol error\n");
		return;
	}

	if (vlen < 7) {
		error("Invalid range received");
		return;
	}

	desc->ch->bs->ns = value[4];
	desc->ch->bs->description = att_get_u16(&value[5]);
}


static void process_batterylevel_desc(struct descriptor *desc)
{
	struct characteristic *ch = desc->ch;
	char uuidstr[MAX_LEN_UUID_STR];
	bt_uuid_t btuuid;

	bt_uuid16_create(&btuuid, GATT_CHARAC_FMT_UUID);

	if (bt_uuid_cmp(&desc->uuid, &btuuid) == 0 && g_strcmp0(ch->attr.uuid,
					BATTERY_LEVEL_UUID) == 0) {
		gatt_read_char(ch->bs->attrib, desc->handle, 0,
						batterylevel_presentation_format_desc_cb, desc);
		return;
	}

	bt_uuid_to_string(&desc->uuid, uuidstr, MAX_LEN_UUID_STR);
	DBG("Ignored descriptor %s in characteristic %s", uuidstr,
								ch->attr.uuid);
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
		process_batterylevel_desc(desc);
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
		ch->bs = bs;

		bs->chars = g_slist_append(bs->chars, ch);

		process_batteryservice_char(ch);

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

static DBusMessage *get_properties(DBusConnection *conn, DBusMessage *msg,
								void *data)
{

	struct batterystate *bs = data;
	DBusMessageIter iter;
	DBusMessageIter dict;
	DBusMessage *reply;

	reply = dbus_message_new_method_return(msg);
	if (reply == NULL)
		return NULL;

	dbus_message_iter_init_append(reply, &iter);

	dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY,
			DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
			DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING
			DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict);

	dict_append_entry(&dict, "Namespace", DBUS_TYPE_BYTE,
							&bs->ns);

	dict_append_entry(&dict, "Description", DBUS_TYPE_UINT16,
							&bs->description);

	dict_append_entry(&dict, "Level", DBUS_TYPE_BYTE,
							&bs->level);

	dbus_message_iter_close_container(&iter, &dict);

	return reply;
}

static GDBusMethodTable battery_methods[] = {
	{ "GetProperties",	"",	"a{sv}",	get_properties },
	{ }
};

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

int batterystate_register(DBusConnection *connection, struct btd_device *device,
					struct att_primary *dattr)
{
	struct batterystate *bs;
	const gchar *device_path = device_get_path(device);
	int err = 0;

	bs = g_new0(struct batterystate, 1);
	bs->conn = dbus_connection_ref(connection);
	bs->dev = btd_device_ref(device);
	bs->svc_range = g_new0(struct att_range, 1);
	bs->svc_range->start = dattr->start;
	bs->svc_range->end = dattr->end;

	bs->path = g_strdup_printf("%s/BATT%04X", device_path, g_slist_length(batteryservices));

	device_add_battery(device,bs->path);

	if (!g_dbus_register_interface(bs->conn, bs->path, BATTERY_INTERFACE,
				battery_methods, NULL,
				NULL, bs, batterystate_free)) {
		error("D-Bus failed to register %s interface",
							BATTERY_INTERFACE);
		batterystate_free(bs);
		err = -EIO;
		goto done;
	}

	batteryservices = g_slist_prepend(batteryservices, bs);

	bs->attioid = btd_device_add_attio_callback(device, attio_connected_cb,
						attio_disconnected_cb, bs);

done:
	return err;
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

	device_remove_battery(device, bs->path);

	g_dbus_unregister_interface(bs->conn, device_get_path(bs->dev),
							BATTERY_INTERFACE);
}
