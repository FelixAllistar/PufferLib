# Invoke from the repository root: make -f ocean/webnav/families/Makefile test-loader
CC = clang-19
CFLAGS = -O2 -std=c11 -Wall -Wextra -Werror
DIR = ocean/webnav/families
BUILD = build/webnav/families/tests

.PHONY: test-loader
test-loader:
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -fPIC -fvisibility=hidden -shared $(DIR)/tests/mock_family.c -o $(BUILD)/mock-a.so
	$(CC) $(CFLAGS) -fPIC -fvisibility=hidden '-DMOCK_NAME="mock-b"' -shared $(DIR)/tests/mock_family.c -o $(BUILD)/mock-b.so
	$(CC) $(CFLAGS) $(DIR)/tests/test_loader.c $(DIR)/common/loader.c -ldl -lm -o $(BUILD)/test_loader
	$(BUILD)/test_loader $(BUILD)/mock-a.so $(BUILD)/mock-b.so
