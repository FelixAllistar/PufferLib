CC = clang-19
CFLAGS = -O2 -std=c11 -Wall -Wextra -Werror
FAMILY ?= panels
EPISODES ?= 20
.PHONY: all family test-loader test browser public-check registry
all: family build/webnav/families/runner
family:
	node ocean/webnav/families/build.cjs $(FAMILY) --test
build/webnav/families/runner: ocean/webnav/families/runner.c ocean/webnav/families/click/expert_synonyms.h $(wildcard ocean/webnav/families/*/public*.h) ocean/webnav/families/common/loader.c ocean/webnav/families/common/loader.h ocean/webnav/families/common/family_api.h
	mkdir -p build/webnav/families
	$(CC) $(CFLAGS) ocean/webnav/families/runner.c ocean/webnav/families/common/loader.c -ldl -lm -o $@
test-loader:
	mkdir -p build/webnav/families/tests
	$(CC) $(CFLAGS) -fPIC -fvisibility=hidden -shared ocean/webnav/families/tests/mock_family.c -o build/webnav/families/tests/mock-a.so
	$(CC) $(CFLAGS) -fPIC -fvisibility=hidden '-DMOCK_NAME="mock-b"' -shared ocean/webnav/families/tests/mock_family.c -o build/webnav/families/tests/mock-b.so
	$(CC) $(CFLAGS) ocean/webnav/families/tests/test_loader.c ocean/webnav/families/common/loader.c -ldl -lm -o build/webnav/families/tests/test_loader
	build/webnav/families/tests/test_loader build/webnav/families/tests/mock-a.so build/webnav/families/tests/mock-b.so
browser: family
	$(CC) $(CFLAGS) -Ivendor ocean/webnav/families/$(FAMILY)/browser_oracle.c ocean/webnav/families/common/loader.c ocean/webnav/cdp.c vendor/cJSON.c -ldl -lm -o build/webnav/families/$(FAMILY)/browser_oracle
	build/webnav/families/$(FAMILY)/browser_oracle $(EPISODES)
public-check: all
	build/webnav/families/runner build/webnav/families/$(FAMILY)/lib$(FAMILY).so $(EPISODES)
registry:
	node ocean/webnav/families/registry.cjs
test: test-loader family public-check
