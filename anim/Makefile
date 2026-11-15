# Motion-matching library: runtime, tests, benchmark and demo.
#
#   make            build build/libmm.a
#   make test       Python builder tests, synthetic database, C runtime tests
#   make bench      build a ~400k-frame synthetic database and time the search
#   make demo       build the raylib stick-figure demo (tools/mm_demo.c)

CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror -ffp-contract=off
PYTHON ?= python3
BUILD := build
RAYLIB ?= $(firstword $(wildcard ../raylib-5.5_linux_amd64 /opt/raylib-5.5_linux_amd64))
BENCH_CLIPS ?= 680

.PHONY: all test pytest ctest bench demo clean

all: $(BUILD)/libmm.a

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/mm.o: mm.c mm.h | $(BUILD)
	$(CC) $(CFLAGS) -c mm.c -o $@

$(BUILD)/libmm.a: $(BUILD)/mm.o
	ar rcs $@ $^

test: pytest ctest

pytest:
	$(PYTHON) tests/test_build.py

$(BUILD)/test.mmdb: tools/mm_build.py tools/mm_convert.py tools/synth_bvh.py tools/bvh.py tools/mmmath.py tools/gltf_skeleton.py tests/make_fixtures.py | $(BUILD)
	$(PYTHON) tests/make_fixtures.py $(BUILD)

$(BUILD)/test_mm: tests/test_mm.c $(BUILD)/libmm.a
	$(CC) $(CFLAGS) tests/test_mm.c $(BUILD)/libmm.a -lm -o $@

ctest: $(BUILD)/test_mm $(BUILD)/test.mmdb
	./$(BUILD)/test_mm $(BUILD)

$(BUILD)/bench/manifest.json: tools/synth_bvh.py | $(BUILD)
	$(PYTHON) tools/synth_bvh.py $(BUILD)/bench --random $(BENCH_CLIPS)

FINGERS := .*Hand(Thumb|Index|Middle|Ring|Pinky).*

# The benchmark database is built once as version 1 and converted to version 2
# (which gives the same bytes as building version 2 directly), with and
# without fingers.
$(BUILD)/bench_v1.mmdb: $(BUILD)/bench/manifest.json $(BUILD)/test.mmdb
	$(PYTHON) tools/mm_build.py --clips $(BUILD)/bench --manifest $(BUILD)/bench/manifest.json \
		--skeleton $(BUILD)/officer.json --map soma77_to_officer --auto-stance 0.7 --format 1 --out $@

$(BUILD)/bench.mmdb: $(BUILD)/bench_v1.mmdb tools/mm_convert.py
	$(PYTHON) tools/mm_convert.py $< $@ --auto-stance 0.7

$(BUILD)/bench_nofingers.mmdb: $(BUILD)/bench_v1.mmdb tools/mm_convert.py
	$(PYTHON) tools/mm_convert.py $< $@ --auto-stance 0.7 --drop-joints '$(FINGERS)'

$(BUILD)/bench_mm: tests/bench_mm.c $(BUILD)/libmm.a
	$(CC) $(CFLAGS) tests/bench_mm.c $(BUILD)/libmm.a -lm -o $@

bench: $(BUILD)/bench_mm $(BUILD)/bench.mmdb $(BUILD)/bench_v1.mmdb $(BUILD)/bench_nofingers.mmdb
	./$(BUILD)/bench_mm $(BUILD)/bench.mmdb $(BUILD)/bench_v1.mmdb $(BUILD)/bench_nofingers.mmdb

$(BUILD)/mm_demo: tools/mm_demo.c $(BUILD)/libmm.a
	$(CC) $(CFLAGS) -I$(RAYLIB)/include tools/mm_demo.c $(BUILD)/libmm.a $(RAYLIB)/lib/libraylib.a \
		-lGL -lm -lpthread -ldl -lrt -lX11 -o $@

demo: $(BUILD)/mm_demo

clean:
	rm -rf $(BUILD)
