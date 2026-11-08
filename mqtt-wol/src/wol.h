/*
 * mqtt-wol — Wake-on-LAN magic packet helper
 * SPDX-License-Identifier: MIT
 */
#ifndef _WOL_H
#define _WOL_H

#include <stdbool.h>
#include <stdint.h>

/* Parse "C8:FF:BF:0D:86:CB", "c8ffbf0d86cb", "C8-FF-..." etc.
 * Returns true on success. */
bool wol_parse_mac(const char *str, uint8_t mac[6]);

/* Send a magic packet to the given MAC.
 * port: UDP port for the packet (e.g. 9 or 7), <= 0 means default 9
 * targets: comma-separated list of IPv4 dests, e.g. "255.255.255.255,192.168.1.255";
 *          NULL/empty means "255.255.255.255"
 * Returns 0 if at least one target accepted the packet, -1 on failure. */
int wol_send(const uint8_t mac[6], int port, const char *targets);

#endif /* _WOL_H */
