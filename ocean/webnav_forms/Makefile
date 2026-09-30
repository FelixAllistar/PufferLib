CC = clang-19
CFLAGS = -O2 -std=c11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
INC = -I. -Isrc -Ivendor -Iraylib-5.5_linux_amd64/include -Iocean/webnav_forms
DIR = ocean/webnav_forms
BUILD = build/webnav_forms
LOADER = ocean/webnav/families/common/loader.c
CDP = ocean/webnav/cdp.c vendor/cJSON.c

.PHONY: test
$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_env: $(DIR)/test_env.c $(DIR)/webnav_forms.h $(LOADER) | $(BUILD)
	$(CC) $(CFLAGS) $(INC) $< $(LOADER) -ldl -lm -o $@

test: $(BUILD)/test_env
	$(BUILD)/test_env

$(BUILD)/browser_eval: $(DIR)/browser_eval.c $(DIR)/webnav_forms.h $(LOADER) $(CDP) src/puffercpu.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) $< $(LOADER) $(CDP) -ldl -lm -o $@

.PHONY: browser-build
browser-build: $(BUILD)/browser_eval
