CORES := $(shell nproc 2>/dev/null || getconf NPROCESSORS_CONF)
BUILD_DIR := build
TARGET := alttab

LOGTYPE ?= NONE

all: release

release:
	cmake -B $(BUILD_DIR) -S . -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j$(CORES)
#Shitty hyprpm..
	cp build/$(TARGET).so .
debug:
	cmake -B $(BUILD_DIR) -S . -DCMAKE_BUILD_TYPE=Debug -DLOG_FLAGS="$(LOGTYPE)"
	cmake --build $(BUILD_DIR) -j$(CORES)
	
run:
	hyprland -c hl.conf
trace:
	HYPRLAND_TRACE=1 hyprland -c hl.conf

install:
	hyprctl plugin unload ~/.config/hypr/plugins/$(TARGET).so
	cp build/$(TARGET).so ~/.config/hypr/plugins/$(TARGET).so
	hyprctl plugin load ~/.config/hypr/plugins/$(TARGET).so

uninstall:
	hyprctl plugin unload ~/.config/hypr/plugins/$(TARGET).so

clean:
	rm -rf build
	rm -f $(TARGET).so
	rm -f compile_commands.json

.PHONY: all release run trace debug
