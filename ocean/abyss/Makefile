ROOT := ../..
CC ?= clang
CPPFLAGS := -I. -I$(ROOT)/src -I$(ROOT)/raylib-5.5_linux_amd64/include
CFLAGS ?= -O2 -std=c17 -Wall -Wextra -Werror

.PHONY: verify test sanitize clean

verify: test sanitize

test: tests/test_abyss
	cd $(ROOT) && ./ocean/abyss/tests/test_abyss

tests/test_abyss: tests/test_abyss.c abyss.h generated_scenarios.h generated_colliders.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -lm -o $@

sanitize: tests/test_abyss_san
	cd $(ROOT) && ASAN_OPTIONS=detect_leaks=1 ./ocean/abyss/tests/test_abyss_san

tests/test_abyss_san: tests/test_abyss.c abyss.h generated_scenarios.h generated_colliders.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer $< -lm -o $@

clean:
	rm -f tests/test_abyss tests/test_abyss_san
