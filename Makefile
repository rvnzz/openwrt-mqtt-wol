# Top-level dev Makefile — convenience targets that do NOT belong to the
# OpenWrt package builds (those are driven by mqtt-wol/Makefile and
# luci-app-mqtt-wol/Makefile via the OpenWrt build system / SDK).
#
#   make test      — native smoke test: send a magic packet to 127.0.0.1
#                    and verify the 102-byte payload with a UDP listener
#   make check     — syntax-check the daemon sources against headers in
#                    /tmp/opencode/inc (downloaded from upstream), if present
#   make clean     — remove build artifacts

WOL_SRC := mqtt-wol/src
CC ?= gcc

.PHONY: test check woltest clean

woltest: tools/woltest.c $(WOL_SRC)/wol.c $(WOL_SRC)/wol.h
	$(CC) -std=gnu11 -Wall -Wextra -I$(WOL_SRC) -o $@ tools/woltest.c $(WOL_SRC)/wol.c

# functional test without root privileges: unicast to localhost
test: woltest
	python3 tools/woltest_listen.py 5 & sleep 0.5; ./woltest c8:ff:bf:0d:86:cb 19009 127.0.0.1; wait

check:
	@test -d /tmp/opencode/inc || { echo "headers not present in /tmp/opencode/inc"; exit 1; }
	$(CC) -std=gnu11 -Wall -Wextra -fsyntax-only \
		-I/tmp/opencode/inc -I/tmp/opencode/inc/libubox -I$(WOL_SRC) \
		$(WOL_SRC)/*.c
	@echo "syntax check OK"

clean:
	rm -f woltest
