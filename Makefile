CORES := $(shell nproc 2>/dev/null || getconf NPROCESSORS_CONF)
BUILD_DIR := build
TARGET := alttab

all: release

release:
	cmake -B $(BUILD_DIR) -S . -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j$(CORES)
debug:
	cmake -B $(BUILD_DIR) -S . -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR) -j$(CORES)
	
run:
	hyprland -c hl.conf
trace:
	HYPRLAND_TRACE=1 hyprland -c hl.conf

run-rethonk:
	~/dev/v_50/rethonk/Hyprland/think/Hyprland -c hl.conf
run-rethonk-trace:
	HYPRLAND_TRACE=1 ~/dev/v_50/rethonk/Hyprland/think/Hyprland -c hl.conf

rethonk:
	PKG_CONFIG_PATH=$$HOME/dev/v_50/rethonk/Hyprland/think \
	cmake -B $(BUILD_DIR) -S . -DCMAKE_BUILD_TYPE=Release
	PKG_CONFIG_PATH=$$HOME/dev/v_50/rethonk/Hyprland/think \
	cmake --build $(BUILD_DIR) -j$(CORES)

install:
	hyprctl plugin unload ~/.config/hypr/plugins/$(TARGET).so
	cp build/$(TARGET).so ~/.config/hypr/plugins/$(TARGET).so
	hyprctl plugin load ~/.config/hypr/plugins/$(TARGET).so

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all release run trace debug clean
