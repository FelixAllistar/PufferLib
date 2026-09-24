# Invoke from the repository root: make -f ocean/webnav/Makefile test
CC = clang-19
CFLAGS = -O3 -std=c11 -Isrc -Ivendor -Iocean/webnav
LIB = build/webnav/libwebnav.a
LIBS = $(LIB) -lm -lpthread
EVAL_SOURCES = ocean/webnav/evaluate.c ocean/webnav/cdp.c ocean/webnav/policy.c vendor/cJSON.c
HEADERS = $(wildcard ocean/webnav/*.h)
NVCC ?= /usr/local/cuda/bin/nvcc
CUDA_ARCH ?= sm_61
CHECKPOINT ?=
EPISODES ?=

.PHONY: all bend test browser setup-browser train test-policy watch eval miniwob test-miniwob test-text-forms test-new-tasks test-first-wave test-dom test-text capture-tools bench-text bench-forms
all: bend
	$(CC) $(CFLAGS) $(EVAL_SOURCES) $(LIBS) -o build/webnav/evaluate
	$(CC) $(CFLAGS) -D_POSIX_C_SOURCE=200809L ocean/webnav/tests/test_native.c $(LIBS) -o build/webnav/test_native
	$(CC) $(CFLAGS) -mavx2 -mfma ocean/webnav/tests/test_env.c $(LIBS) -o build/webnav/test_env
bend:
	bash ocean/webnav/build.sh
test: all
	build/webnav/test_native
	build/webnav/test_env
	build/webnav/evaluate native 300 - 128 2 100000 expert
setup-browser:
	bash ocean/webnav/setup_browser.sh
watch:
	bash ocean/webnav/run.sh watch "$(if $(CHECKPOINT),$(CHECKPOINT),latest)" "$(if $(EPISODES),$(EPISODES),20)"
eval:
	bash ocean/webnav/run.sh eval "$(if $(CHECKPOINT),$(CHECKPOINT),latest)" "$(if $(EPISODES),$(EPISODES),1000)"
miniwob:
	bash ocean/webnav/setup_miniwob.sh
	bash ocean/webnav/miniwob/build.sh
test-miniwob: miniwob
	build/webnav/test_miniwob
	build/webnav/test_generated
	build/webnav/test_text_forms
	build/webnav/test_miniwob_boundary
	build/webnav/miniwob_oracle click-button 1000
	build/webnav/miniwob_oracle click-checkboxes 1000
	build/webnav/miniwob_oracle click-link 1000
	build/webnav/miniwob_oracle focus-text 1000
	build/webnav/miniwob_oracle click-option 1000
	WEBNAV_GENERATED_FORMS=1 build/webnav/miniwob_oracle click-checkboxes 1000
	WEBNAV_GENERATED_FORMS=1 build/webnav/miniwob_oracle click-option 1000
test-text-forms: miniwob
	build/webnav/test_text_forms
	build/webnav/text_oracle enter-text 200
	build/webnav/text_oracle login-user 200
	build/webnav/text_oracle read-table 200
test-new-tasks: miniwob
	timeout -s KILL 60s build/webnav/test_sequence_select
	timeout -s KILL 60s build/webnav/test_tree
	timeout -s KILL 60s build/webnav/test_autocomplete
	timeout -s KILL 60s build/webnav/test_task_routing
	build/webnav/test_miniwob_boundary
	build/webnav/sequence_select_oracle 200
	build/webnav/tree_oracle 200
	build/webnav/autocomplete_oracle 200
test-first-wave: test-miniwob test-text-forms test-new-tasks
test-dom:
	$(CC) $(CFLAGS) ocean/webnav/tests/test_dom.c ocean/webnav/dom.c ocean/webnav/cdp.c vendor/cJSON.c -lm -o build/webnav/test_dom
	build/webnav/test_dom
bench-forms: miniwob
	$(CC) $(CFLAGS) ocean/webnav/tools/bench_forms.c build/webnav/libminiwob.a -lpthread -lm -o build/webnav/bench_forms
	build/webnav/bench_forms
test-text:
	bash ocean/webnav/tools/text_tests.sh
capture-tools:
	$(CC) $(CFLAGS) ocean/webnav/tools/capture_dom.c ocean/webnav/dom.c ocean/webnav/cdp.c vendor/cJSON.c -lm -o build/webnav/capture_dom
bench-text:
	node ocean/webnav/tools/site_corpus.cjs
	$(CC) $(CFLAGS) ocean/webnav/tools/bench_text_encoder.c ocean/webnav/text_encoder.c ocean/webnav/text_cache.c vendor/cJSON.c -licuuc -lm -o build/webnav/bench_text_encoder
	build/webnav/bench_text_encoder build/webnav/reference/site-text-corpus.json
browser: all
	bash ocean/webnav/tests/browser_cases.sh
train:
	NATIVE_OUTPUT_NAME=build/webnav/puffer HEADLESS=1 bash build.sh webnav --float
	build/webnav/puffer train webnav base.checkpoint_dir=build/webnav/checkpoints base.log_dir=build/webnav/logs
test-policy: all
	test -n "$(CHECKPOINT)"
	$(CC) $(CFLAGS) -c ocean/webnav/policy.c -o build/webnav/policy.o
	$(NVCC) -O2 -arch=$(CUDA_ARCH) -std=c++17 -I. -Isrc -Ivendor -I/usr/include \
	    ocean/webnav/tests/test_policy.cu build/webnav/policy.o $(LIBS) \
	    -L/usr/local/cuda/lib64 -L/usr/lib/x86_64-linux-gnu \
	    -lcudart -lnccl -lnvidia-ml -lcublas -lcusolver -lcurand -o build/webnav/test_policy
	build/webnav/test_policy "$(CHECKPOINT)"

.PHONY: training-tools test-training train-dom test-training-policy
training-tools:
	bash ocean/webnav/training_build.sh
test-training: training-tools
	$(CC) $(CFLAGS) ocean/webnav/tests/test_training.c build/webnav/libtraining.a build/webnav/libminiwob.a -licuuc -lpthread -lm -o build/webnav/test_training
	timeout -s KILL 120s build/webnav/test_training
train-dom:
	NATIVE_OUTPUT_NAME=build/webnav/puffer_dom HEADLESS=1 bash build.sh webnav_dom --float
	build/webnav/puffer_dom train webnav_dom base.checkpoint_dir=build/webnav/training/checkpoints base.log_dir=build/webnav/training/logs
test-training-policy:
	test -n "$(CHECKPOINT)"
	$(NVCC) -O2 -arch=$(CUDA_ARCH) -std=c++17 -I. -Isrc -Ivendor -I/usr/include \
	    ocean/webnav/tests/test_training_policy.cu build/webnav/training/training_policy.o build/webnav/libtraining.a build/webnav/libminiwob.a \
	    -L/usr/local/cuda/lib64 -L/usr/lib/x86_64-linux-gnu -licuuc \
	    -lcudart -lnccl -lnvidia-ml -lcublas -lcusolver -lcurand -lpthread -lm -o build/webnav/test_training_policy
	build/webnav/test_training_policy "$(CHECKPOINT)"
.PHONY: bench-training
bench-training: training-tools
	$(CC) $(CFLAGS) ocean/webnav/tools/bench_training.c build/webnav/libtraining.a build/webnav/libminiwob.a -licuuc -lpthread -lm -o build/webnav/bench_training
	build/webnav/bench_training 1000 0
	build/webnav/bench_training 1000 1
