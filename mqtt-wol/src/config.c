/*
 * mqtt-wol — UCI config loading
 * SPDX-License-Identifier: MIT
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <uci.h>

#include "config.h"
#include "wol.h"

static const char *opt_str(struct uci_context *ctx, struct uci_section *s,
                           const char *name, const char *def)
{
	const char *v = uci_lookup_option_string(ctx, s, name);
	return (v && *v) ? v : def;
}

static int opt_bool(struct uci_context *ctx, struct uci_section *s,
                    const char *name, int def)
{
	const char *v = uci_lookup_option_string(ctx, s, name);
	if (!v)
		return def;
	if (!strcmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "on") ||
	    !strcasecmp(v, "yes"))
		return 1;
	if (!strcmp(v, "0") || !strcasecmp(v, "false") || !strcasecmp(v, "off") ||
	    !strcasecmp(v, "no"))
		return 0;
	return def;
}

static int opt_int(struct uci_context *ctx, struct uci_section *s,
                   const char *name, int def)
{
	const char *v = uci_lookup_option_string(ctx, s, name);
	char *end;
	long n;

	if (!v || !*v)
		return def;
	n = strtol(v, &end, 10);
	if (end == v)
		return def;
	return (int)n;
}

static void copy_str(char *dst, size_t dstsz, const char *src)
{
	snprintf(dst, dstsz, "%s", src ? src : "");
}

int wol_cfg_load(struct wol_cfg *cfg)
{
	struct uci_context *ctx;
	struct uci_package *pkg = NULL;
	struct uci_element *e;
	int rc = -1;

	memset(cfg, 0, sizeof(*cfg));

	/* sane defaults */
	cfg->port = 1883;
	cfg->keepalive = 60;
	cfg->default_port = 9;
	copy_str(cfg->default_broadcast, sizeof(cfg->default_broadcast),
	         "255.255.255.255");
	copy_str(cfg->clientid, sizeof(cfg->clientid), "openwrt-mqtt-wol");

	ctx = uci_alloc_context();
	if (!ctx)
		return -1;

	if (uci_load(ctx, "mqtt-wol", &pkg) != UCI_OK) {
		fprintf(stderr, "config: cannot load uci package 'mqtt-wol' (err %d), is /etc/config/mqtt-wol present?\n",
		        ctx->err);
		goto out;
	}

	uci_foreach_element(&pkg->sections, e) {
		struct uci_section *s = uci_to_section(e);

		if (!strcmp(s->type, "broker")) {
			copy_str(cfg->host, sizeof(cfg->host),
			         opt_str(ctx, s, "host", ""));
			cfg->port = opt_int(ctx, s, "port", 1883);
			copy_str(cfg->username, sizeof(cfg->username),
			         opt_str(ctx, s, "username", ""));
			copy_str(cfg->password, sizeof(cfg->password),
			         opt_str(ctx, s, "password", ""));
			copy_str(cfg->clientid, sizeof(cfg->clientid),
			         opt_str(ctx, s, "clientid", "openwrt-mqtt-wol"));
			cfg->tls = opt_bool(ctx, s, "tls", 0);
			cfg->keepalive = opt_int(ctx, s, "keepalive", 60);
			copy_str(cfg->status_topic, sizeof(cfg->status_topic),
			         opt_str(ctx, s, "status_topic", ""));
			copy_str(cfg->ca_file, sizeof(cfg->ca_file),
			         opt_str(ctx, s, "ca_file", ""));
			cfg->debug = opt_bool(ctx, s, "debug", 0);
			copy_str(cfg->default_broadcast, sizeof(cfg->default_broadcast),
			         opt_str(ctx, s, "broadcast", "255.255.255.255"));
			cfg->default_port = opt_int(ctx, s, "wol_port", 9);
		} else if (!strcmp(s->type, "device")) {
			struct wol_device *d;
			uint8_t mac[6];

			if (cfg->num_devices >= WOL_MAX_DEVICES) {
				fprintf(stderr, "config: too many devices (max %u)\n",
				        WOL_MAX_DEVICES);
				continue;
			}
			d = &cfg->devices[cfg->num_devices];

			copy_str(d->name, sizeof(d->name),
			         opt_str(ctx, s, "name", e->name));
			copy_str(d->mac, sizeof(d->mac),
			         opt_str(ctx, s, "mac", ""));
			copy_str(d->topic, sizeof(d->topic),
			         opt_str(ctx, s, "topic", ""));
			d->port = opt_int(ctx, s, "port", 0);
			copy_str(d->broadcast, sizeof(d->broadcast),
			         opt_str(ctx, s, "broadcast", ""));

			if (!wol_parse_mac(d->mac, mac)) {
				fprintf(stderr, "config: device '%s': invalid mac '%s', skipped\n",
				        d->name, d->mac);
				continue;
			}
			if (!*d->topic) {
				fprintf(stderr, "config: device '%s': empty topic, skipped\n",
				        d->name);
				continue;
			}
			cfg->num_devices++;
		}
	}

	rc = 0;
	uci_unload(ctx, pkg);
out:
	uci_free_context(ctx);
	return rc;
}

void wol_cfg_print(const struct wol_cfg *cfg)
{
	int i;

	printf("broker: %s:%d%s user='%s' clientid='%s' keepalive=%d\n",
	       cfg->host, cfg->port, cfg->tls ? " (tls)" : "",
	       cfg->username, cfg->clientid, cfg->keepalive);
	printf("wol defaults: port=%d broadcast='%s'\n",
	       cfg->default_port, cfg->default_broadcast);
	printf("status_topic: '%s'\n", cfg->status_topic);
	printf("devices (%d):\n", cfg->num_devices);
	for (i = 0; i < cfg->num_devices; i++) {
		const struct wol_device *d = &cfg->devices[i];
		printf("  [%d] name='%s' mac='%s' topic='%s' port=%d broadcast='%s'\n",
		       i, d->name, d->mac, d->topic, d->port, d->broadcast);
	}
}
