CC = clang-19
CFLAGS = -O2 -std=c11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -mavx2 -mfma
PUBLIC = ocean/webnav/unified/capabilities.c ocean/webnav/unified/caps_basic.c \
 ocean/webnav/unified/caps_text.c ocean/webnav/unified/caps_widgets.c \
 ocean/webnav/unified/caps_pointer.c ocean/webnav/unified/policy.c \
 ocean/webnav/unified/semantic.c ocean/webnav/text_encoder.c vendor/cJSON.c
.PHONY: all
all:
	node ocean/webnav/families/resource_guard.cjs
	mkdir -p build/webnav/benchmarks
	$(CC) $(CFLAGS) -Isrc -Ivendor -Iraylib-5.5_linux_amd64/include \
	 ocean/webnav/benchmarks/policy_rpc.c $(PUBLIC) -licuuc -lpthread -lm \
	 -o build/webnav/benchmarks/policy_rpc
	$(CC) $(CFLAGS) ocean/webnav/benchmarks/response_rpc.c vendor/cJSON.c \
	 -Lbuild/webnav/primitives/response_form -lresponse_form \
	 -Wl,-rpath,'$$ORIGIN/../primitives/response_form' -lm \
	 -o build/webnav/benchmarks/response_rpc
	$(CC) $(CFLAGS) ocean/webnav/benchmarks/browser_controls_rpc.c vendor/cJSON.c \
	 -Lbuild/webnav/primitives/browser_contexts -lbrowser_contexts \
	 -Wl,-rpath,'$$ORIGIN/../primitives/browser_contexts' -lm \
	 -o build/webnav/benchmarks/browser_controls_rpc
