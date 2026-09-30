CC = clang-19
CFLAGS = -O2 -std=c11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
INC = -I. -Isrc -Ivendor -Iraylib-5.5_linux_amd64/include -Iocean/webnav_navigation
DIR = ocean/webnav_navigation
BUILD = build/webnav_navigation
LOADER = ocean/webnav/families/common/loader.c
CDP = ocean/webnav/cdp.c vendor/cJSON.c

$(BUILD):
	mkdir -p $@
$(BUILD)/test_env: $(DIR)/test_env.c $(DIR)/webnav_navigation.h $(LOADER) | $(BUILD)
	$(CC) $(CFLAGS) $(INC) $< $(LOADER) -ldl -lm -o $@
test: $(BUILD)/test_env
	$(BUILD)/test_env
$(BUILD)/native_eval: $(DIR)/native_eval.c $(DIR)/webnav_navigation.h $(LOADER) src/puffercpu.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) $< $(LOADER) -ldl -lm -o $@
$(BUILD)/browser_eval: $(DIR)/browser_eval.c $(DIR)/webnav_navigation.h $(LOADER) $(CDP) src/puffercpu.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) $< $(LOADER) $(CDP) -ldl -lm -o $@
browser-build: $(BUILD)/browser_eval
.PHONY: test browser-build
