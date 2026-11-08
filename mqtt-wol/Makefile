#
# mqtt-wol — MQTT to Wake-on-LAN bridge daemon
#
include $(TOPDIR)/rules.mk

PKG_NAME:=mqtt-wol
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

PKG_MAINTAINER:=rzzx
PKG_LICENSE:=MIT

include $(INCLUDE_DIR)/package.mk

define Package/mqtt-wol
  SECTION:=net
  CATEGORY:=Network
  SUBMENU:=IP Addresses and Names
  TITLE:=MQTT to Wake-on-LAN bridge daemon
  # libmosquitto-ssl: feed name on 21.02+ (TLS via mbedtls/openssl);
  # libmosquitto-nossl would work too if TLS is never enabled in config
  DEPENDS:=+libmosquitto-ssl +libuci +libubus +libblobmsg-json +libpthread
endef

define Package/mqtt-wol/description
 Subscribes to configurable MQTT topics and sends Wake-on-LAN magic
 packets when a wake command ("1", "on", "wake") arrives. Designed to
 replace an ESP8266-based WoL bridge: the router is always online and
 already sits in the right broadcast domain. Registers a ubus object
 "mqtt-wol" (status/wake/reload) for use by LuCI or scripts.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(TARGET_CC) $(TARGET_CFLAGS) $(TARGET_CPPFLAGS) -std=gnu11 \
		-o $(PKG_BUILD_DIR)/mqtt-wol \
		$(PKG_BUILD_DIR)/*.c \
		$(TARGET_LDFLAGS) \
		-lmosquitto -lubus -lubox -lblobmsg_json -luci -lpthread
endef

define Package/mqtt-wol/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/mqtt-wol $(1)/usr/sbin/

	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/mqtt-wol.init $(1)/etc/init.d/mqtt-wol

	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/mqtt-wol.config $(1)/etc/config/mqtt-wol
endef

define Package/mqtt-wol/conffiles
/etc/config/mqtt-wol
endef

$(eval $(call BuildPackage,mqtt-wol))
