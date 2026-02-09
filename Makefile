CORES := $(shell nproc 2>/dev/null || getconf NPROCESSORS_CONF)
BUILD_DIR := build
TARGET := tabcarousel

all: release

release:
	cmake -B $(BUILD_DIR) -S . -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j$(CORES)

run:
	hyprland -c hl.conf

trace:
	HYPRLAND_TRACE=1 hyprland -c hl.conf
debug:
	HYPRLAND_TRACE=1 hyprland -c hl.conf

.PHONY: all release run trace debug
