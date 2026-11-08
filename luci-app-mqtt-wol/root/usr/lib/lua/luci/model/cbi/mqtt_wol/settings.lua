local m, s, o

m = Map("mqtt-wol",
	translate("MQTT Wake-On-LAN"),
	translate("The router listens on MQTT topics and sends Wake-on-LAN "
		.. "magic packets to the configured devices. Publishing '1' (or "
		.. "'on' / 'wake') to a device topic wakes it up; devices can also "
		.. "be woken manually from the Status tab."))

-- ----------------------------------------------------------------------
-- broker
-- ----------------------------------------------------------------------
s = m:section(NamedSection, "main", "broker", translate("MQTT broker"))
s.description = translate("Connection settings for the MQTT broker.")

o = s:option(Flag, "enabled", translate("Enable"))
o.rmempty = false
o.default = "1"

o = s:option(Value, "host", translate("Broker host"),
	translate("Hostname or IP address, e.g. m8.wqtt.ru"))
o.rmempty = false

o = s:option(Value, "port", translate("Broker port"))
o.rmempty = false
o.datatype = "port"
o.default = "1883"

o = s:option(Value, "username", translate("Username"))
o.optional = true

o = s:option(Value, "password", translate("Password"))
o.optional = true

o = s:option(Value, "clientid", translate("Client ID"),
	translate("Must be unique on the broker; defaults to openwrt-mqtt-wol."))
o.optional = true
o.default = "openwrt-mqtt-wol"

o = s:option(Value, "keepalive", translate("Keepalive (s)"))
o.optional = true
o.datatype = "range(5,3600)"

o = s:option(Flag, "tls", translate("Use TLS"))
o.rmempty = false
o.default = "0"
o.description = translate("Requires the libmosquitto-ssl package (installed by default).")

o = s:option(Value, "ca_file", translate("CA bundle"),
	translate("Path to a CA certificate bundle, e.g. /etc/ssl/ca-certificates.crt"))
o.optional = true
o:depends("tls", "1")

o = s:option(Value, "status_topic", translate("Status topic (optional)"),
	translate("Retained online/offline JSON about this daemon is published here."))
o.optional = true

-- ----------------------------------------------------------------------
-- global WOL defaults
-- ----------------------------------------------------------------------
s = m:section(NamedSection, "main", "broker", translate("Wake-on-LAN defaults"))

o = s:option(Value, "wol_port", translate("UDP port"),
	translate("Destination port of the magic packet. Default: 9."))
o.optional = true
o.datatype = "port"

o = s:option(Value, "broadcast", translate("Default broadcast address"),
	translate("One or more comma-separated targets, e.g. 255.255.255.255,192.168.1.255. "
		.. "Per-device settings override this."))
o.optional = true
o.default = "255.255.255.255"

o = s:option(Flag, "debug", translate("Debug log"))
o.rmempty = false
o.default = "0"

-- ----------------------------------------------------------------------
-- devices
-- ----------------------------------------------------------------------
s = m:section(TypedSection, "device", translate("Devices"),
	translate("Each device has its own MQTT topic. Any wake command arriving there "
		.. "triggers the magic packet."))
s.anonymous = true
s.addremove = true
s.template = "cbi/tblsection"
s.sortable = false

o = s:option(Value, "name", translate("Name"))
o.rmempty = false
o.placeholder = translate("Office PC")

o = s:option(Value, "mac", translate("MAC address"))
o.rmempty = false
o.datatype = "macaddr"
o.placeholder = "AC:DE:48:00:11:22"

o = s:option(Value, "topic", translate("MQTT topic"))
o.rmempty = false
o.placeholder = "control/pcwol"
o.description = translate("May contain MQTT wildcards (+/#). On hosted brokers "
	.. "(e.g. clusterfly) use the full topic including the user prefix.")

o = s:option(Value, "port", translate("Port"))
o.optional = true
o.datatype = "port"
o.description = translate("Leave empty for default.")

o = s:option(Value, "broadcast", translate("Broadcast address"))
o.optional = true
o.description = translate("Comma-separated; leave empty for default.")

return m
