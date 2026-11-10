BUILD_DIR ?= build

.PHONY: all clean test

all:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. && $(MAKE)

clean:
	@if [ -d $(BUILD_DIR) ]; then $(MAKE) -C $(BUILD_DIR) clean; fi

test: all
	@cd $(BUILD_DIR) && ./test_screw_inspector
