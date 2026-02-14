CORES := $(shell nproc 2>/dev/null || getconf NPROCESSORS_CONF)
BUILD_DIR := build
TARGET := alttab

all: release

release:
	cmake -B $(BUILD_DIR) -S . -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j$(CORES)

.PHONY: all release
