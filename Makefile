CC      := i686-w64-mingw32-gcc
CFLAGS  := -O2 -Wall -Wextra -Werror -masm=intel -fno-ident
LDFLAGS := -shared -static-libgcc -Wl,--enable-stdcall-fixup -Wl,--kill-at -s

BUILD_DIR    := build
TARGET       := $(BUILD_DIR)/DeadSpaceFovFix.dll
TARGET_PROXY := $(BUILD_DIR)/dinput8.dll
SRC          := src/main.c
DEF          := src/exports.def
CONFIG       := DeadSpaceFovFix.ini
MOUSEFIX_DLL := DeadSpaceMouseFix/dinput8.dll

GAME_DIR    ?= $(HOME)/.local/share/Steam/steamapps/common/Dead Space
REMOTE_DIR  := $(GAME_DIR)/dinput8_Remote

.PHONY: all clean install install-with-mousefix install-standalone uninstall

all: $(TARGET) $(TARGET_PROXY)

$(TARGET): $(SRC) $(DEF)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SRC) $(DEF) -o $(TARGET) $(LDFLAGS)

$(TARGET_PROXY): $(TARGET)
	cp -f $(TARGET) $(TARGET_PROXY)

# Default install: alongside Dead Space Mouse Fix (via dinput8_Remote/)
install: install-with-mousefix

install-with-mousefix: $(TARGET)
	@if [ -f "$(MOUSEFIX_DLL)" ]; then \
		cp -f "$(MOUSEFIX_DLL)" "$(GAME_DIR)/dinput8.dll"; \
		echo "Ensured Dead Space Mouse Fix is installed at $(GAME_DIR)/dinput8.dll"; \
	fi
	mkdir -p "$(REMOTE_DIR)"
	cp -f "$(TARGET)" "$(REMOTE_DIR)/DeadSpaceFovFix.dll"
	@if [ ! -f "$(REMOTE_DIR)/$(CONFIG)" ]; then \
		if [ -f "$(GAME_DIR)/$(CONFIG)" ]; then \
			cp "$(GAME_DIR)/$(CONFIG)" "$(REMOTE_DIR)/$(CONFIG)"; \
			echo "Migrated $(CONFIG) from $(GAME_DIR)/ to $(REMOTE_DIR)/"; \
		else \
			cp "$(CONFIG)" "$(REMOTE_DIR)/$(CONFIG)"; \
			echo "Installed default $(CONFIG) to $(REMOTE_DIR)/"; \
		fi; \
	else \
		echo "Keeping existing $(REMOTE_DIR)/$(CONFIG)"; \
	fi
	@rm -f "$(GAME_DIR)/$(CONFIG)" "$(GAME_DIR)/DeadSpaceFovFix.log"
	@echo "Installed DeadSpaceFovFix (with Mouse Fix) to $(REMOTE_DIR)/"

# Standalone install: directly as <game_dir>/dinput8.dll without Dead Space Mouse Fix
install-standalone: $(TARGET_PROXY)
	cp -f "$(TARGET_PROXY)" "$(GAME_DIR)/dinput8.dll"
	@if [ ! -f "$(GAME_DIR)/$(CONFIG)" ]; then \
		if [ -f "$(REMOTE_DIR)/$(CONFIG)" ]; then \
			cp "$(REMOTE_DIR)/$(CONFIG)" "$(GAME_DIR)/$(CONFIG)"; \
			echo "Migrated $(CONFIG) from $(REMOTE_DIR)/ to $(GAME_DIR)/"; \
		else \
			cp "$(CONFIG)" "$(GAME_DIR)/$(CONFIG)"; \
			echo "Installed default $(CONFIG) to $(GAME_DIR)/"; \
		fi; \
	else \
		echo "Keeping existing $(GAME_DIR)/$(CONFIG)"; \
	fi
	@rm -f "$(REMOTE_DIR)/DeadSpaceFovFix.dll" "$(REMOTE_DIR)/$(CONFIG)" "$(REMOTE_DIR)/DeadSpaceFovFix.log"
	@rmdir --ignore-fail-on-non-empty "$(REMOTE_DIR)" 2>/dev/null || true
	@echo "Installed DeadSpaceFovFix (standalone) as $(GAME_DIR)/dinput8.dll"

uninstall:
	rm -f "$(REMOTE_DIR)/DeadSpaceFovFix.dll" "$(REMOTE_DIR)/DeadSpaceFovFix.log"
	rm -f "$(GAME_DIR)/DeadSpaceFovFix.log"
	@if [ -f "$(GAME_DIR)/dinput8.dll" ] && [ -f "$(TARGET)" ] && cmp -s "$(GAME_DIR)/dinput8.dll" "$(TARGET)"; then \
		rm -f "$(GAME_DIR)/dinput8.dll"; \
		echo "Removed standalone $(GAME_DIR)/dinput8.dll"; \
	fi

clean:
	rm -rf $(BUILD_DIR)
