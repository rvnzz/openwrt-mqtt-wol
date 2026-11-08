#
# luci-app-mqtt-wol — LuCI frontend for the mqtt-wol daemon
#
# Builds two ways:
#   1. inside a tree with the luci feed present (feeds/luci/luci.mk) →
#      full LUCI_* metadata, automatic luci-i18n-* sub-packages
#   2. bare SDK / no luci feed → plain fallback package (UI strings
#      stay in English, install luci-base separately)
#
include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-mqtt-wol
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

PKG_MAINTAINER:=rzzx
PKG_LICENSE:=MIT

LUCI_TITLE:=LuCI support for MQTT Wake-On-LAN
# luci-compat pulls luci-lua-runtime: required for classic Lua/CBI apps on
# OpenWrt 25.12+, where base LuCI is ucode-only
LUCI_DEPENDS:=+mqtt-wol +luci-compat
LUCI_PKGARCH:=all

ifneq ($(wildcard $(TOPDIR)/feeds/luci/luci.mk),)

include $(TOPDIR)/feeds/luci/luci.mk

else

define Package/luci-app-mqtt-wol
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=$(LUCI_TITLE)
  DEPENDS:=$(LUCI_DEPENDS)
  PKGARCH:=$(LUCI_PKGARCH)
endef

define Package/luci-app-mqtt-wol/description
 Web UI (LuCI) for configuring and operating the mqtt-wol daemon:
 MQTT broker settings, per-device wake topics, live status page with
 wake buttons.
endef

define Package/luci-app-mqtt-wol/install
	$(INSTALL_DIR) $(1)
	$(CP) ./root/* $(1)/
endef

$(eval $(call BuildPackage,luci-app-mqtt-wol))

endif
