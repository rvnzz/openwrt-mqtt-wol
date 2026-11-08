/*
 * mqtt-wol — Wake-on-LAN magic packet helper
 * SPDX-License-Identifier: MIT
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "wol.h"

bool wol_parse_mac(const char *str, uint8_t mac[6])
{
	unsigned int v[6];
	int i, n = 0;

	if (!str)
		return false;

	/* colon-separated form: aa:bb:cc:dd:ee:ff (also accept - and .) */
	if (sscanf(str, "%2x:%2x:%2x:%2x:%2x:%2x",
	           &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
		for (i = 0; i < 6; i++)
			mac[i] = (uint8_t)v[i];
		return true;
	}
	if (sscanf(str, "%2x-%2x-%2x-%2x-%2x-%2x",
	           &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
		for (i = 0; i < 6; i++)
			mac[i] = (uint8_t)v[i];
		return true;
	}

	/* bare form: 12 hex digits, any case */
	for (i = 0; str[i]; i++) {
		if (!isxdigit((unsigned char)str[i]))
			return false;
	}
	if (i != 12)
		return false;

	for (i = 0; i < 6; i++) {
		char byte[3] = { str[i * 2], str[i * 2 + 1], 0 };
		mac[i] = (uint8_t)strtoul(byte, NULL, 16);
	}
	(void)n;
	return true;
}

static int wol_send_one(const uint8_t mac[6], int port, const char *addr)
{
	uint8_t pkt[6 + 16 * 6];
	struct sockaddr_in sa;
	int fd, on = 1, rc;

	memset(pkt, 0xff, 6);
	for (int i = 0; i < 16; i++)
		memcpy(pkt + 6 + i * 6, mac, 6);

	memset(&sa, 0, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, addr, &sa.sin_addr) != 1)
		return -1;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return -1;

	setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));

	rc = sendto(fd, pkt, sizeof(pkt), 0,
	            (struct sockaddr *)&sa, sizeof(sa));
	close(fd);

	if (rc < 0) {
		fprintf(stderr, "wol: sendto(%s:%d) failed: %s\n",
		        addr, port, strerror(errno));
		return -1;
	}
	return 0;
}

int wol_send(const uint8_t mac[6], int port, const char *targets)
{
	char *dup, *saveptr, *tok;
	int rc = -1;

	if (port <= 0)
		port = 9;

	dup = strdup((targets && *targets) ? targets : "255.255.255.255");
	if (!dup)
		return -1;

	for (tok = strtok_r(dup, ",; \t", &saveptr);
	     tok;
	     tok = strtok_r(NULL, ",; \t", &saveptr)) {
		if (wol_send_one(mac, port, tok) == 0) {
			printf("wol: magic packet for %02x:%02x:%02x:%02x:%02x:%02x -> %s:%d\n",
			       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
			       tok, port);
			rc = 0;
		}
	}

	free(dup);
	return rc;
}
