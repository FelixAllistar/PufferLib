ROOT := ../..
CC ?= cc
RAYLIB := $(ROOT)/raylib-5.5_linux_amd64
CPPFLAGS := -I$(ROOT)/src -I$(RAYLIB)/include
CFLAGS ?= -O2 -std=gnu11
LDLIBS := $(RAYLIB)/lib/libraylib.a -lm -lGL -ldl -lpthread

.PHONY: viewer test
build:
	mkdir -p build

viewer: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) robocode.c $(LDLIBS) -o build/viewer

test: | build
	$(CC) $(CPPFLAGS) $(CFLAGS) test_viewer.c $(LDLIBS) -o build/test_viewer
	cd $(ROOT) && ./ocean/robocode/build/test_viewer
