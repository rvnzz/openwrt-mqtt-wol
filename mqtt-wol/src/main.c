/*
 * mqtt-wol — MQTT-to-Wake-on-LAN bridge daemon for OpenWrt
 * SPDX-License-Identifier: MIT
 *
 * Subscribes to per-device MQTT topics; when a wake command arrives
 * ("1", "on", "wake" or "true"), sends a WoL magic packet over UDP.
 * Also exposes a ubus object so LuCI/scripts can control it:
 *
 *   ubus call mqtt-wol status
 *   ubus call mqtt-wol wake '{"target":"PC"}'                    # by name
 *   ubus call mqtt-wol wake '{"target":"c8:ff:bf:0d:86:cb"}'     # by MAC
 *   ubus call mqtt-wol reload
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* strcasecmp, localtime_r, strtok_r on strict libcs */
#endif

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#include <pthread.h>

#include <libubox/blobmsg.h>
#include <libubox/uloop.h>
#include <libubox/utils.h>
#include <libubus.h>
#include <mosquitto.h>

#include "config.h"
#include "wol.h"

#define PROG "mqtt-wol"
#define DEFAULT_CA_FILE "/etc/ssl/ca-certificates.crt"

/* ------------------------------------------------------------------ */
/* globals                                                            */

static struct wol_cfg cfg;
static pthread_mutex_t cfg_lock = PTHREAD_MUTEX_INITIALIZER;

static struct mosquitto *mosq;
static pthread_mutex_t mosq_lock = PTHREAD_MUTEX_INITIALIZER;

static volatile bool ev_loop_run = true;
static volatile bool pending_reload = false;
static volatile bool mqtt_connected = false;

static time_t started_at;
static volatile uint64_t wake_count;
static char last_wake[128];
static volatile time_t last_wake_ts;

static struct blob_buf b;
static struct ubus_context *ubus_ctx;
static struct uloop_timeout ubus_retry;

/* ------------------------------------------------------------------ */
/* helpers                                                            */

static void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void log_msg(const char *fmt, ...)
{
	va_list ap;
	time_t t = time(NULL);
	struct tm tm;

	localtime_r(&t, &tm);

	fprintf(stderr, "%04d-%02d-%02d %02d:%02d:%02d mqtt-wol: ",
	        tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
	        tm.tm_hour, tm.tm_min, tm.tm_sec);

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fflush(stderr);
}

static void trim_lower(char *s)
{
	char *p = s;
	size_t n;

	while (*p == ' ' || *p == '\t')
		p++;
	if (p != s)
		memmove(s, p, strlen(p) + 1);

	n = strlen(s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' ||
	                s[n - 1] == '\r' || s[n - 1] == '\n'))
		s[--n] = 0;

	for (p = s; *p; p++)
		*p = (char)tolower((unsigned char)*p);
}

/* payload accepted as "wake now" command */
static bool payload_is_wake(const void *payload, size_t len)
{
	char buf[32];

	if (len == 0 || len >= sizeof(buf))
		return false;
	memcpy(buf, payload, len);
	buf[len] = 0;
	trim_lower(buf);

	return !strcmp(buf, "1") || !strcmp(buf, "on") ||
	       !strcmp(buf, "wake") || !strcmp(buf, "true");
}

static bool mac_equal(const char *a, const char *bstr)
{
	uint8_t m1[6], m2[6];

	return wol_parse_mac(a, m1) && wol_parse_mac(bstr, m2) &&
	       !memcmp(m1, m2, sizeof(m1));
}

static void note_wake(const char *what)
{
	wake_count++;
	last_wake_ts = time(NULL);
	snprintf(last_wake, sizeof(last_wake), "%s", what);
}

/* send magic packet for one device; caller must not hold cfg_lock */
static int wake_device(const struct wol_device *d)
{
	uint8_t mac[6];
	int port;
	char bc[128];

	if (!wol_parse_mac(d->mac, mac)) {
		log_msg("device '%s': invalid mac '%s'", d->name, d->mac);
		return -1;
	}

	port = d->port > 0 ? d->port : cfg.default_port;
	snprintf(bc, sizeof(bc), "%s",
	         d->broadcast[0] ? d->broadcast : cfg.default_broadcast);

	return wol_send(mac, port, bc);
}

/* ------------------------------------------------------------------ */
/* mosquitto callbacks (run on mosquitto's network thread)            */

static void pub_status_locked(const char *state)
{
	char buf[256];

	if (!cfg.status_topic[0])
		return;

	snprintf(buf, sizeof(buf), "{\"client\":\"%s\",\"state\":\"%s\"}",
	         cfg.clientid, state);
	mosquitto_publish(mosq, NULL, cfg.status_topic,
	                  (int)strlen(buf), buf, 0, true);
}

static void on_connect(struct mosquitto *m, void *ud, int rc)
{
	int i;

	(void)ud;

	if (rc != MOSQ_ERR_SUCCESS) {
		log_msg("mqtt: broker refused connection (code %d: %s)",
		        rc, mosquitto_connack_string(rc));
		mqtt_connected = false;
		return;
	}

	mqtt_connected = true;
	log_msg("mqtt: connected to %s:%d", cfg.host, cfg.port);

	pthread_mutex_lock(&cfg_lock);
	for (i = 0; i < cfg.num_devices; i++) {
		if (mosquitto_subscribe(m, NULL, cfg.devices[i].topic, 0) !=
		    MOSQ_ERR_SUCCESS)
			log_msg("mqtt: subscribe to '%s' failed",
			        cfg.devices[i].topic);
	}
	pub_status_locked("online");
	pthread_mutex_unlock(&cfg_lock);
}

static void on_disconnect(struct mosquitto *m, void *ud, int rc)
{
	(void)m;
	(void)ud;

	mqtt_connected = false;
	log_msg("mqtt: disconnected (code %d), retrying automatically", rc);
}

static void on_log(struct mosquitto *m, void *ud, int level, const char *str)
{
	(void)m;
	(void)ud;
	(void)level;
	if (cfg.debug)
		log_msg("libmosquitto: %s", str);
}

static void on_message(struct mosquitto *m, void *ud,
                       const struct mosquitto_message *msg)
{
	struct wol_device d;
	bool matched = false, is_cmd;
	int i, found = -1, rc = -1;

	(void)m;
	(void)ud;

	if (!msg->payload || msg->payloadlen <= 0)
		return;

	is_cmd = payload_is_wake(msg->payload, (size_t)msg->payloadlen);

	pthread_mutex_lock(&cfg_lock);
	for (i = 0; i < cfg.num_devices; i++) {
		if (mosquitto_topic_matches_sub(cfg.devices[i].topic, msg->topic,
		                                &matched) == MOSQ_ERR_SUCCESS &&
		    matched) {
			d = cfg.devices[i];
			found = i;
			break;
		}
	}
	if (found >= 0 && is_cmd)
		rc = wake_device(&d);
	pthread_mutex_unlock(&cfg_lock);

	if (found < 0)
		return;

	if (rc == 0) {
		note_wake(d.name);
		log_msg("wake '%s' (%s) triggered via topic '%s'",
		        d.name, d.mac, msg->topic);
	} else if (!is_cmd) {
		if (cfg.debug)
			log_msg("ignored non-wake payload on '%s'", msg->topic);
	} else {
		log_msg("wake '%s' failed", d.name);
	}
}

/* ------------------------------------------------------------------ */
/* mosquitto lifecycle                                                */

static void mqtt_stop(void)
{
	pthread_mutex_lock(&mosq_lock);
	if (mosq) {
		/* announce offline while we still have a live connection;
		 * the will itself only fires on abnormal disconnects */
		if (mqtt_connected) {
			pthread_mutex_lock(&cfg_lock);
			pub_status_locked("offline");
			pthread_mutex_unlock(&cfg_lock);
		}
		mosquitto_disconnect(mosq);
		/* don't hold cfg_lock here: callbacks may want it */
		mosquitto_loop_stop(mosq, false);
		mosquitto_destroy(mosq);
		mosq = NULL;
	}
	mqtt_connected = false;
	pthread_mutex_unlock(&mosq_lock);
}

static int mqtt_start(void)
{
	struct mosquitto *m;
	char host[128], cafile[128], user[128], pass[128], clientid[64];
	char status_topic[192];
	int port, keepalive, rc;
	bool tls;

	/* snapshot config under the lock */
	pthread_mutex_lock(&cfg_lock);
	snprintf(host, sizeof(host), "%s", cfg.host);
	port = cfg.port;
	keepalive = cfg.keepalive;
	snprintf(user, sizeof(user), "%s", cfg.username);
	snprintf(pass, sizeof(pass), "%s", cfg.password);
	snprintf(clientid, sizeof(clientid), "%s", cfg.clientid);
	snprintf(cafile, sizeof(cafile), "%s",
	         cfg.ca_file[0] ? cfg.ca_file : DEFAULT_CA_FILE);
	snprintf(status_topic, sizeof(status_topic), "%s", cfg.status_topic);
	tls = cfg.tls;
	pthread_mutex_unlock(&cfg_lock);

	if (!host[0]) {
		log_msg("mqtt: no broker host configured, not starting");
		return -1;
	}

	m = mosquitto_new(clientid[0] ? clientid : PROG, true, NULL);
	if (!m) {
		log_msg("mqtt: mosquitto_new failed: %s", strerror(errno));
		return -1;
	}

	mosquitto_connect_callback_set(m, on_connect);
	mosquitto_disconnect_callback_set(m, on_disconnect);
	mosquitto_message_callback_set(m, on_message);
	mosquitto_log_callback_set(m, on_log);

	if (tls) {
		rc = mosquitto_tls_set(m, cafile, NULL, NULL, NULL, NULL);
		if (rc != MOSQ_ERR_SUCCESS)
			log_msg("mqtt: warning: tls setup failed: %s",
			        mosquitto_strerror(rc));
	}

	if (user[0])
		mosquitto_username_pw_set(m, user, pass[0] ? pass : NULL);

	if (status_topic[0]) {
		static const char offline[] = "{\"offline\":true}";

		mosquitto_will_set(m, status_topic,
		                   (int)sizeof(offline) - 1, offline, 0, true);
	}

	mosquitto_reconnect_delay_set(m, 2, 30, true);

	rc = mosquitto_connect(m, host, port, keepalive);
	if (rc != MOSQ_ERR_SUCCESS)
		log_msg("mqtt: initial connect to %s:%d: %s (retrying in background)",
		        host, port, mosquitto_strerror(rc));

	rc = mosquitto_loop_start(m);
	if (rc != MOSQ_ERR_SUCCESS) {
		log_msg("mqtt: loop_start failed: %s", mosquitto_strerror(rc));
		mosquitto_destroy(m);
		return -1;
	}

	pthread_mutex_lock(&mosq_lock);
	mosq = m;
	pthread_mutex_unlock(&mosq_lock);

	return 0;
}

/* re-read uci config and restart the mqtt client */
static void do_reload(void)
{
	struct wol_cfg ncfg;

	if (wol_cfg_load(&ncfg) != 0) {
		log_msg("reload: uci config load failed, keeping old config");
		return;
	}

	log_msg("reload: broker %s:%d, %d device(s)",
	        ncfg.host, ncfg.port, ncfg.num_devices);

	mqtt_stop();

	pthread_mutex_lock(&cfg_lock);
	cfg = ncfg;
	pthread_mutex_unlock(&cfg_lock);

	mqtt_start();
}

/* ------------------------------------------------------------------ */
/* ubus interface                                                     */

static int handle_status(struct ubus_context *ctx, struct ubus_object *obj,
                         struct ubus_request_data *req, const char *method,
                         struct blob_attr *msg)
{
	void *c, *d;
	int i;

	(void)obj;
	(void)method;
	(void)msg;

	blob_buf_init(&b, 0);
	blobmsg_add_u8(&b, "connected", mqtt_connected);
	blobmsg_add_string(&b, "host", cfg.host);
	blobmsg_add_u32(&b, "port", cfg.port);
	blobmsg_add_u32(&b, "uptime", (uint32_t)(time(NULL) - started_at));
	blobmsg_add_u64(&b, "wake_count", wake_count);

	if (last_wake_ts) {
		c = blobmsg_open_table(&b, "last_wake");
		blobmsg_add_string(&b, "target", last_wake);
		blobmsg_add_u64(&b, "ts", (uint64_t)last_wake_ts);
		blobmsg_close_table(&b, c);
	}

	pthread_mutex_lock(&cfg_lock);
	c = blobmsg_open_array(&b, "devices");
	for (i = 0; i < cfg.num_devices; i++) {
		d = blobmsg_open_table(&b, NULL);
		blobmsg_add_string(&b, "name", cfg.devices[i].name);
		blobmsg_add_string(&b, "mac", cfg.devices[i].mac);
		blobmsg_add_string(&b, "topic", cfg.devices[i].topic);
		blobmsg_add_u32(&b, "port",
		                 cfg.devices[i].port > 0 ? cfg.devices[i].port
		                                         : cfg.default_port);
		blobmsg_close_table(&b, d);
	}
	blobmsg_close_array(&b, c);
	pthread_mutex_unlock(&cfg_lock);

	ubus_send_reply(ctx, req, b.head);
	return UBUS_STATUS_OK;
}

static const struct blobmsg_policy wake_policy[] = {
	{ .name = "target", .type = BLOBMSG_TYPE_STRING },
};

/* find a device by name or mac; if target looks like a mac and no
 * device matches it, synthesize an ad-hoc device with global defaults */
static bool find_target(const char *target, struct wol_device *out)
{
	char tmac[32];
	bool found = false;
	int i;

	snprintf(tmac, sizeof(tmac), "%s", target);
	trim_lower(tmac);

	pthread_mutex_lock(&cfg_lock);
	for (i = 0; i < cfg.num_devices; i++) {
		if (!strcasecmp(cfg.devices[i].name, target) ||
		    mac_equal(cfg.devices[i].mac, tmac)) {
			*out = cfg.devices[i];
			found = true;
			break;
		}
	}
	if (!found && strchr(target, ':')) {
		uint8_t probe[6];

		if (wol_parse_mac(target, probe)) {
			/* raw MAC address, no configured device matched:
			 * send with global defaults */
			memset(out, 0, sizeof(*out));
			snprintf(out->name, sizeof(out->name), "%s", target);
			snprintf(out->mac, sizeof(out->mac), "%s", target);
			found = true;
		}
	}
	pthread_mutex_unlock(&cfg_lock);

	return found;
}

static int handle_wake(struct ubus_context *ctx, struct ubus_object *obj,
                       struct ubus_request_data *req, const char *method,
                       struct blob_attr *msg)
{
	struct blob_attr *tb[ARRAY_SIZE(wake_policy)];
	struct wol_device dev;
	const char *target;

	(void)obj;
	(void)method;

	blob_buf_init(&b, 0);

	if (blobmsg_parse(wake_policy, ARRAY_SIZE(wake_policy), tb,
	                  blob_data(msg), blob_len(msg)) || !tb[0]) {
		blobmsg_add_string(&b, "result", "ERROR");
		blobmsg_add_string(&b, "message", "usage: '{\"target\":\"<name or mac>\"}'");
		ubus_send_reply(ctx, req, b.head);
		return UBUS_STATUS_OK;
	}

	target = blobmsg_data(tb[0]);

	if (!find_target(target, &dev)) {
		blobmsg_add_string(&b, "result", "ERROR");
		blobmsg_add_string(&b, "message", "device not found");
		ubus_send_reply(ctx, req, b.head);
		/* return OK: ubus CLI drops the reply body on non-zero status */
		return UBUS_STATUS_OK;
	}

	if (wake_device(&dev) == 0) {
		note_wake(dev.name);
		blobmsg_add_string(&b, "result", "OK");
		blobmsg_add_string(&b, "target", dev.name);
	} else {
		blobmsg_add_string(&b, "result", "ERROR");
		blobmsg_add_string(&b, "message", "send failed");
		blobmsg_add_string(&b, "target", dev.name);
	}

	ubus_send_reply(ctx, req, b.head);
	return UBUS_STATUS_OK;
}

static int handle_reload(struct ubus_context *ctx, struct ubus_object *obj,
                         struct ubus_request_data *req, const char *method,
                         struct blob_attr *msg)
{
	(void)obj;
	(void)method;
	(void)msg;

	pending_reload = true;
	uloop_end(); /* make uloop_run() return so the reload runs on the main thread */

	blob_buf_init(&b, 0);
	blobmsg_add_string(&b, "result", "OK");
	ubus_send_reply(ctx, req, b.head);
	return UBUS_STATUS_OK;
}

static const struct ubus_method mqtt_wol_methods[] = {
	UBUS_METHOD("wake", handle_wake, wake_policy),
	UBUS_METHOD_NOARG("status", handle_status),
	UBUS_METHOD_NOARG("reload", handle_reload),
};

static struct ubus_object_type mqtt_wol_obj_type =
	UBUS_OBJECT_TYPE(PROG, mqtt_wol_methods);

static struct ubus_object ubus_obj = {
	.name = PROG,
	.type = &mqtt_wol_obj_type,
};

static int ubus_attach(void)
{
	int rc;

	if (ubus_ctx) {
		ubus_remove_object(ubus_ctx, &ubus_obj);
		ubus_free(ubus_ctx);
		ubus_ctx = NULL;
	}

	ubus_ctx = ubus_connect(NULL);
	if (!ubus_ctx)
		return -1;

	ubus_add_uloop(ubus_ctx);

	rc = ubus_add_object(ubus_ctx, &ubus_obj);
	if (rc != UBUS_STATUS_OK) {
		log_msg("ubus: add_object failed: %s", ubus_strerror(rc));
		ubus_free(ubus_ctx);
		ubus_ctx = NULL;
		return -1;
	}

	return 0;
}

static void ubus_retry_cb(struct uloop_timeout *t)
{
	if (ubus_attach() != 0) {
		log_msg("ubus: connect failed, retrying in 2s");
		uloop_timeout_set(t, 2000);
	}
}

/* ------------------------------------------------------------------ */
/* signals / main                                                     */

static void on_signal(int sig)
{
	if (sig == SIGHUP)
		pending_reload = true;
	else
		ev_loop_run = false;
	uloop_end();
}

static void usage(const char *argv0)
{
	fprintf(stderr,
	        "Usage: %s [-c] [-h]\n"
	        "  -c, --print-config  print parsed uci config and exit\n"
	        "  -h, --help          show this help\n"
	        "\nThe daemon reads /etc/config/mqtt-wol and runs in foreground\n"
	        "(intended to be supervised by procd).\n",
	        argv0);
}

int main(int argc, char **argv)
{
	static const struct option long_opts[] = {
		{ "print-config", no_argument, NULL, 'c' },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 },
	};
	struct sigaction sa, sa_pipe;
	int opt;

	while ((opt = getopt_long(argc, argv, "ch", long_opts, NULL)) != -1) {
		switch (opt) {
		case 'c':
			if (wol_cfg_load(&cfg) != 0)
				return EXIT_FAILURE;
			wol_cfg_print(&cfg);
			return EXIT_SUCCESS;
		default:
			usage(argv[0]);
			return opt == 'h' ? EXIT_SUCCESS : EXIT_FAILURE;
		}
	}

	if (wol_cfg_load(&cfg) != 0)
		return EXIT_FAILURE;

	if (cfg.num_devices == 0)
		log_msg("warning: no devices configured");

	started_at = time(NULL);

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGHUP, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	memset(&sa_pipe, 0, sizeof(sa_pipe));
	sa_pipe.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa_pipe, NULL);

	if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS) {
		log_msg("mosquitto_lib_init failed");
		return EXIT_FAILURE;
	}

	if (uloop_init() != 0) {
		log_msg("uloop_init failed");
		return EXIT_FAILURE;
	}

	mqtt_start();

	ubus_retry.cb = ubus_retry_cb;
	if (ubus_attach() != 0)
		uloop_timeout_set(&ubus_retry, 1000);

	log_msg(PROG " started, %d device(s)", cfg.num_devices);

	while (ev_loop_run) {
		uloop_cancelled = false;
		uloop_run();

		if (pending_reload && ev_loop_run) {
			pending_reload = false;
			do_reload();
		}
	}

	log_msg(PROG " stopping");
	mqtt_stop();
	if (ubus_ctx) {
		ubus_remove_object(ubus_ctx, &ubus_obj);
		ubus_free(ubus_ctx);
	}
	uloop_done();
	mosquitto_lib_cleanup();

	return EXIT_SUCCESS;
}
