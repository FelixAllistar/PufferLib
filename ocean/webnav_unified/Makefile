# Invoke via run.sh from repository root: verified serial memory-limited scope.
CC = clang-19
CFLAGS = -O2 -std=c11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter
INC = -Isrc -Ivendor -Iraylib-5.5_linux_amd64/include -Iocean/webnav_unified
BUILD = build/webnav_unified
PUBLIC = ocean/webnav/unified/capabilities.c ocean/webnav/unified/caps_basic.c \
 ocean/webnav/unified/caps_text.c ocean/webnav/unified/caps_widgets.c \
 ocean/webnav/unified/caps_pointer.c ocean/webnav/unified/policy.c \
 ocean/webnav/unified/semantic.c ocean/webnav/text_encoder.c vendor/cJSON.c
LOADER = ocean/webnav/families/common/loader.c
.PHONY: test build evaluator semantic-probe
test:
	node ocean/webnav/families/resource_guard.cjs
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(INC) ocean/webnav/unified/test_policy.c $(PUBLIC) -licuuc -lpthread -lm -o $(BUILD)/test_policy
	$(BUILD)/test_policy
	$(CC) $(CFLAGS) $(INC) ocean/webnav_unified/test_env.c $(PUBLIC) $(LOADER) -ldl -licuuc -lpthread -lm -o $(BUILD)/test_env
	$(BUILD)/test_env
	$(CC) $(CFLAGS) $(INC) ocean/webnav/unified/test_semantic.c ocean/webnav/unified/semantic.c ocean/webnav/text_encoder.c vendor/cJSON.c -licuuc -lpthread -lm -o $(BUILD)/test_semantic
	$(BUILD)/test_semantic
	$(CC) $(CFLAGS) -mavx2 -mfma $(INC) ocean/webnav_unified/test_cpu_linear.c $(PUBLIC) $(LOADER) -ldl -licuuc -lpthread -lm -o $(BUILD)/test_cpu_linear
	$(BUILD)/test_cpu_linear
build:
	node ocean/webnav/families/resource_guard.cjs
	mkdir -p $(BUILD)
	bash build.sh webnav_unified $(BUILD)/puffer
evaluator:
	node ocean/webnav/families/resource_guard.cjs
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -mavx2 -mfma $(INC) ocean/webnav_unified/test_cpu_linear.c $(PUBLIC) $(LOADER) -ldl -licuuc -lpthread -lm -o $(BUILD)/test_cpu_linear
	$(BUILD)/test_cpu_linear
	$(CC) $(CFLAGS) -mavx2 -mfma $(INC) ocean/webnav_unified/native_eval.c $(PUBLIC) $(LOADER) -ldl -licuuc -lpthread -lm -o $(BUILD)/native_eval
semantic-probe:
	node ocean/webnav/families/resource_guard.cjs
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(INC) ocean/webnav/unified/semantic_probe.c ocean/webnav/unified/semantic.c ocean/webnav/text_encoder.c vendor/cJSON.c -licuuc -lpthread -lm -o $(BUILD)/semantic_probe
	$(BUILD)/semantic_probe
