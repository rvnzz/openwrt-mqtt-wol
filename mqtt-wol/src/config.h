/*
 * mqtt-wol — configuration read from UCI (/etc/config/mqtt-wol)
 * SPDX-License-Identifier: MIT
 */
#ifndef _CONFIG_H
#define _CONFIG_H

#include <stdbool.h>

#define WOL_MAX_DEVICES 64

struct wol_device {
	char name[64];       /* logical name; also used by `ubus wake` */
	char mac[32];        /* target MAC address */
	char topic[192];     /* MQTT topic to subscribe (may contain +/#) */
	int port;            /* UDP port of magic packet, 0 = global default */
	char broadcast[128]; /* destination(s), empty = global default */
};

struct wol_cfg {
	/* MQTT broker */
	char host[128];
	int port;
	char username[128];
	char password[128];
	char clientid[64];
	bool tls;
	int keepalive;

	/* availability reporting (optional) */
	char status_topic[192]; /* retained online/offline publish, empty = off */

	/* TLS (optional) */
	char ca_file[128];      /* CA bundle path, empty = system default */

	/* global WOL defaults */
	int default_port;
	char default_broadcast[128];

	struct wol_device devices[WOL_MAX_DEVICES];
	int num_devices;

	bool debug;
};

/* Load config from UCI package "mqtt-wol". Returns 0 on success,
 * -1 if the package could not be read. Missing options keep defaults. */
int wol_cfg_load(struct wol_cfg *cfg);

/* Dump the loaded config to stdout (for `mqtt-wol --print-config`). */
void wol_cfg_print(const struct wol_cfg *cfg);

#endif /* _CONFIG_H */
