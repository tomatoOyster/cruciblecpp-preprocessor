BUILD_DIR ?= build

.PHONY: all configure test clean

all: configure
	cmake --build $(BUILD_DIR)

configure:
	cmake -S . -B $(BUILD_DIR)

test: all
	ctest --test-dir $(BUILD_DIR) --output-on-failure

clean:
	cmake -E remove_directory $(BUILD_DIR)
