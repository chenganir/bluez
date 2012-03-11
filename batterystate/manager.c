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

#include <glib.h>
#include <errno.h>
#include <bluetooth/uuid.h>

#include "adapter.h"
#include "device.h"
#include "att.h"
#include "gattrib.h"
#include "gatt.h"
#include "batterystate.h"
#include "manager.h"

#define BATTERY_SERVICE_UUID		"0000180f-0000-1000-8000-00805f9b34fb"

static int batterystate_driver_probe(struct btd_device *device, GSList *uuids)
{
	return batterystate_register(device);
}

static void batterystate_driver_remove(struct btd_device *device)
{
	batterystate_unregister(device);
}

static struct btd_device_driver battery_device_driver = {
	.name	= "battery-driver",
	.uuids	= BTD_UUIDS(BATTERY_SERVICE_UUID),
	.probe	= batterystate_driver_probe,
	.remove	= batterystate_driver_remove
};

int batterystate_manager_init(void)
{
	return btd_register_device_driver(&battery_device_driver);
}

void batterystate_manager_exit(void)
{
	btd_unregister_device_driver(&battery_device_driver);
}
