ROOT := ../..
CC ?= clang
CPPFLAGS := -I. -I$(ROOT)/src -I$(ROOT)/raylib-5.5_linux_amd64/include
CFLAGS ?= -O2 -std=c17 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function

.PHONY: verify test sanitize catalog-test rules clean

verify: test sanitize catalog-test tiers-test

catalog-test:
	python3 tests/test_catalog.py

test: tests/test_abyss
	cd $(ROOT) && ./ocean/abyss/tests/test_abyss

tests/test_abyss: tests/test_abyss.c tests/test_npc_repairs.h tests/legacy_dark_profile.ini abyss.h generated_scenarios.h generated_combat_catalog.h generated_mechanics.h npc_mechanics.h generated_colliders.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -lm -o $@

sanitize: tests/test_abyss_san
	cd $(ROOT) && ASAN_OPTIONS=detect_leaks=1 ./ocean/abyss/tests/test_abyss_san

tests/test_abyss_san: tests/test_abyss.c tests/test_npc_repairs.h tests/legacy_dark_profile.ini abyss.h generated_scenarios.h generated_combat_catalog.h generated_mechanics.h npc_mechanics.h generated_colliders.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer $< -lm -o $@

rules: tests/eval_rules
	cd $(ROOT) && ./ocean/abyss/tests/eval_rules

tests/eval_rules: tests/eval_rules.c abyss.h generated_scenarios.h generated_combat_catalog.h generated_mechanics.h npc_mechanics.h generated_colliders.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -lm -o $@

clean:
	rm -f tests/test_abyss tests/test_abyss_san tests/eval_rules

.PHONY: tiers-test
tiers-test:
	python3 tests/test_combat_catalog.py
	python3 tests/test_checkpoint_upgrade.py
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_tiers.c -lm -o /tmp/abyss_tiers_test
	cd $(ROOT) && /tmp/abyss_tiers_test
