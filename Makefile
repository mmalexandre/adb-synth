BUILD_DIR := build
CONFIG := Release
STANDALONE_APP := $(BUILD_DIR)/AdbSynth_artefacts/Standalone/AdbSynth

.PHONY: all configure build run clean

all: build

configure:
	cmake -B $(BUILD_DIR) -S .

build: configure
	cmake --build $(BUILD_DIR) --config $(CONFIG)

run: build
	$(STANDALONE_APP)

clean:
	rm -rf $(BUILD_DIR)