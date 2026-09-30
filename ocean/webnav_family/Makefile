# Invoke from the repository root. Bend family code generation has its own
# resource-limited builder; these targets compile only the C adapter/tests.
CC = clang-19
CFLAGS = -O2 -std=c11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
INC = -I. -Isrc -Ivendor -Iraylib-5.5_linux_amd64/include -Iocean/webnav_family
DIR = ocean/webnav_family
BUILD = build/webnav_family
LOADER = ocean/webnav/families/common/loader.c
CDP = ocean/webnav/cdp.c vendor/cJSON.c
TEXT = ocean/webnav/text_encoder.c vendor/cJSON.c

.PHONY: test random browser-build
$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/test_env: $(DIR)/test_env.c $(DIR)/webnav_family.h $(LOADER) $(TEXT) | $(BUILD)
	$(CC) $(CFLAGS) $(INC) $< $(LOADER) $(TEXT) -ldl -licuuc -lm -o $@

test: $(BUILD)/test_env
	$(BUILD)/test_env

$(BUILD)/random_baseline: $(DIR)/random_baseline.c $(DIR)/webnav_family.h $(LOADER) $(TEXT) | $(BUILD)
	$(CC) $(CFLAGS) $(INC) $< $(LOADER) $(TEXT) -ldl -licuuc -lm -o $@

random: $(BUILD)/random_baseline
	$(BUILD)/random_baseline

$(BUILD)/browser_eval: $(DIR)/browser_eval.c $(DIR)/webnav_family.h $(LOADER) $(CDP) $(TEXT) src/puffercpu.c | $(BUILD)
	$(CC) $(CFLAGS) $(INC) $< $(LOADER) $(CDP) ocean/webnav/text_encoder.c -ldl -licuuc -lm -o $@

browser-build: $(BUILD)/browser_eval

$(BUILD)/semantic_probe: $(DIR)/semantic_probe.c $(TEXT) | $(BUILD)
	$(CC) $(CFLAGS) $(INC) $< $(TEXT) -licuuc -lm -o $@

.PHONY: semantic-probe
semantic-probe: $(BUILD)/semantic_probe
	$(BUILD)/semantic_probe
