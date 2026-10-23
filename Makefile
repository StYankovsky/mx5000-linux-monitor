
CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wpedantic -std=c11

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

BUILD_DIR := build
UPSTREAM_SRC := third_party/mx5000tools
UPSTREAM_BUILD := $(BUILD_DIR)/mx5000tools
UPSTREAM_PREFIX := $(abspath $(UPSTREAM_BUILD)/install)

MONITOR_BIN := $(BUILD_DIR)/mx5000-monitor
TOOL_BIN := $(UPSTREAM_PREFIX)/bin/mx5000-tool
LIB_FILE := $(UPSTREAM_PREFIX)/lib/libmx5000.so.0.0.0

.PHONY: all upstream monitor clean install uninstall test-local

all: upstream monitor

upstream:
	mkdir -p "$(UPSTREAM_BUILD)"
	cd "$(UPSTREAM_SRC)" && autoreconf -fi
	cd "$(UPSTREAM_BUILD)" && \
	"$(abspath $(UPSTREAM_SRC))/configure" \
	--prefix="$(UPSTREAM_PREFIX)" --disable-static
	$(MAKE) -C "$(UPSTREAM_BUILD)"
	env -u DESTDIR $(MAKE) -C "$(UPSTREAM_BUILD)" install

monitor: $(MONITOR_BIN)

$(MONITOR_BIN): src/mx5000-monitor.c
	mkdir -p "$(BUILD_DIR)"
	$(CC) $(CFLAGS) -o "$@" "$<"

test-local: all
	@test -x "$(MONITOR_BIN)"
	@test -x "$(TOOL_BIN)"
	@"$(TOOL_BIN)" --help >/dev/null
	@echo "Local build checks passed."
	@echo "Monitor: $(MONITOR_BIN)"
	@echo "LCD tool: $(TOOL_BIN)"
	@echo 'Manual test: "$(MONITOR_BIN)" -t "$(TOOL_BIN)"'

install: all
	@command -v chrpath >/dev/null || \
	{ echo "ERROR: install chrpath first"; exit 1; }
	install -Dm755 "$(MONITOR_BIN)" \
	"$(DESTDIR)$(BINDIR)/mx5000-monitor"
	install -Dm755 "$(TOOL_BIN)" \
	"$(DESTDIR)$(BINDIR)/mx5000-tool"
	chrpath -d "$(DESTDIR)$(BINDIR)/mx5000-tool"
	install -Dm755 "$(LIB_FILE)" \
	"$(DESTDIR)$(PREFIX)/lib/libmx5000.so.0.0.0"
	ln -sfn libmx5000.so.0.0.0 \
	"$(DESTDIR)$(PREFIX)/lib/libmx5000.so.0"
	ln -sfn libmx5000.so.0 \
	"$(DESTDIR)$(PREFIX)/lib/libmx5000.so"

uninstall:
	@test "$(CONFIRM_UNINSTALL)" = "YES" || \
	{ echo "Refusing uninstall. Set CONFIRM_UNINSTALL=YES explicitly."; exit 1; }
	rm -f "$(DESTDIR)$(BINDIR)/mx5000-monitor"
	rm -f "$(DESTDIR)$(BINDIR)/mx5000-tool"
	rm -f "$(DESTDIR)$(PREFIX)/lib/libmx5000.so"
	rm -f "$(DESTDIR)$(PREFIX)/lib/libmx5000.so.0"
	rm -f "$(DESTDIR)$(PREFIX)/lib/libmx5000.so.0.0.0"

clean:
	rm -rf "$(BUILD_DIR)"
