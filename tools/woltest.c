/*
 * woltest — tiny standalone Wake-on-LAN sender for testing
 * build: cc -std=gnu11 -o woltest woltest.c wol.c
 * usage: woltest <mac> [port] [target1,target2,...]
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wol.h"

int main(int argc, char **argv)
{
	uint8_t mac[6];
	int port = 9;
	char *targets = NULL;

	if (argc < 2) {
		fprintf(stderr, "usage: %s <mac> [port] [targets]\n"
		        "  e.g. %s C8:FF:BF:0D:86:CB 9 192.168.1.255,255.255.255.255\n",
		        argv[0], argv[0]);
		return 2;
	}

	if (!wol_parse_mac(argv[1], mac)) {
		fprintf(stderr, "invalid MAC: %s\n", argv[1]);
		return 2;
	}

	if (argc > 2)
		port = atoi(argv[2]);
	if (argc > 3)
		targets = argv[3];

	return wol_send(mac, port, targets) == 0 ? 0 : 1;
}
