# Invoke from the repository root: make -f ocean/webnav/families/Makefile test-loader
CC = clang-19
CFLAGS = -O2 -std=c11 -Wall -Wextra -Werror
DIR = ocean/webnav/families
BUILD = build/webnav/families/tests
FAMILY ?= panels
EPISODES ?= 20

.PHONY: public-check test
build/webnav/families/runner: $(DIR)/runner.c $(DIR)/common/loader.c $(DIR)/common/loader.h $(DIR)/common/family_api.h $(wildcard $(DIR)/*/public*.h) $(DIR)/click/expert_synonyms.h
	mkdir -p build/webnav/families
	$(CC) $(CFLAGS) $(DIR)/runner.c $(DIR)/common/loader.c -ldl -lm -o $@

public-check: family build/webnav/families/runner
	build/webnav/families/runner build/webnav/families/$(FAMILY)/lib$(FAMILY).so $(EPISODES)

test: test-loader public-check

.PHONY: family browser
family:
	node $(DIR)/build.cjs $(FAMILY) --test

browser: family
	$(CC) $(CFLAGS) -Ivendor $(DIR)/$(FAMILY)/browser_oracle.c $(DIR)/common/loader.c ocean/webnav/cdp.c vendor/cJSON.c -ldl -lm -o build/webnav/families/$(FAMILY)/browser_oracle
	build/webnav/families/$(FAMILY)/browser_oracle $(EPISODES)

.PHONY: test-loader
test-loader:
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -fPIC -fvisibility=hidden -shared $(DIR)/tests/mock_family.c -o $(BUILD)/mock-a.so
	$(CC) $(CFLAGS) -fPIC -fvisibility=hidden '-DMOCK_NAME="mock-b"' -shared $(DIR)/tests/mock_family.c -o $(BUILD)/mock-b.so
	$(CC) $(CFLAGS) $(DIR)/tests/test_loader.c $(DIR)/common/loader.c -ldl -lm -o $(BUILD)/test_loader
	$(BUILD)/test_loader $(BUILD)/mock-a.so $(BUILD)/mock-b.so
