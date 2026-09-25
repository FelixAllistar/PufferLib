ROOT := ../..
BUILD := $(ROOT)/build/webnav
CC := clang-19
CFLAGS := -O3 -std=c11 -D_POSIX_C_SOURCE=200809L
INCLUDES := -I. -I$(ROOT)/src -I$(ROOT)/vendor -I$(ROOT)/raylib-5.5_linux_amd64/include

.PHONY: bridge test
EVAL_SOURCES := evaluate.c cdp.c policy.c $(ROOT)/vendor/cJSON.c

.PHONY: evaluator browser-test
evaluator: $(BUILD)/evaluate

$(BUILD)/evaluate: $(EVAL_SOURCES) policy.h cdp.h observation.h $(BUILD)/libwebnav.a
	$(CC) $(CFLAGS) $(INCLUDES) $(EVAL_SOURCES) $(BUILD)/libwebnav.a -lm -lpthread -o $@

browser-test: evaluator
	bash tests/browser_cases.sh

.PHONY: dom-test
DOM_SOURCES := tests/test_dom.c dom.c cdp.c $(ROOT)/vendor/cJSON.c

$(BUILD)/test_dom: $(DOM_SOURCES) dom.h cdp.h web/dom_snapshot.js
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(DOM_SOURCES) -lm -o $@

dom-test: $(BUILD)/test_dom
	cd $(ROOT) && build/webnav/test_dom

.PHONY: text-test
text-test:
	cd $(ROOT) && mkdir -p build/webnav && bash ocean/webnav/tools/text_tests.sh

bridge:
	bash build.sh

$(BUILD)/test_native: tests/test_native.c bridge.h $(BUILD)/libwebnav.a
	$(CC) $(CFLAGS) -I. $< $(BUILD)/libwebnav.a -lm -lpthread -o $@

$(BUILD)/test_env: tests/test_env.c webnav.h observation.h bridge.h $(BUILD)/libwebnav.a
	$(CC) $(CFLAGS) -mavx2 -mfma $(INCLUDES) $< $(BUILD)/libwebnav.a -lm -lpthread -o $@

test: $(BUILD)/test_native $(BUILD)/test_env
	$(BUILD)/test_native
	$(BUILD)/test_env
