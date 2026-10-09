# Run from the repository root: make -f ocean/mario_fpg_time/Makefile curriculum
OUT ?= build/mario_fpg_time
SRC := ocean/mario_fpg_time
CPU := build/mario_sim/runtime/logic_cpu.a
MODULE := build/mario_sim/runtime/cuda_replay.cubin
VERIFY := build/mario_sim/runtime/verify_cuda
NVCC ?= /usr/local/cuda/bin/nvcc
.PHONY: all table runtime test pipe transfer curriculum curriculum-test semantic-test
all: $(OUT)/build_table
runtime:
	$(MAKE) -f ocean/mario_sim/runtime.mk -j2 cpu gpu
$(CPU) $(MODULE) $(VERIFY) &:
	$(MAKE) -f ocean/mario_sim/runtime.mk -j2 cpu gpu
build/retro/libquicknes.a:
	$(MAKE) -C ocean/retro library
$(OUT)/build_table: $(SRC)/build_table.cpp $(SRC)/table.h $(SRC)/time_reward.h $(SRC)/pipe_start.h $(SRC)/curriculum.h $(CPU) $(wildcard ocean/mario_sim/*.h)
	mkdir -p $(OUT)
	clang++ -O3 -std=c++17 -Iocean/mario_sim $< $(CPU) -o $@
table: $(OUT)/build_table $(MODULE) $(OUT)/curriculum/bank.bin
	$(OUT)/build_table
$(OUT)/build_pipe_bank: $(SRC)/build_pipe_bank.cpp $(SRC)/pipe_start.h $(SRC)/rom_check.h $(wildcard ocean/mario_sim/*.h) $(CPU) build/retro/libquicknes.a
	mkdir -p $(OUT)
	clang++ -O2 -std=c++17 -Iocean/mario_sim -Isrc -Iraylib-5.5_linux_amd64/include -Iocean/retro/nes_emu $< $(CPU) build/retro/libquicknes.a raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lm -ldl -lpthread -fopenmp -o $@
pipe: $(OUT)/build_pipe_bank $(MODULE) $(VERIFY) $(OUT)/build_table
	$(OUT)/build_pipe_bank $(OUT)/pipe
	$(VERIFY) $(OUT)/pipe/validation/clips.bin > $(OUT)/pipe/cuda_validation.json
	$(OUT)/build_table --bank $(OUT)/pipe/bank.bin --teacher $(OUT)/pipe/teacher/clips.bin --output $(OUT)/pipe_targets.bin --variants 0 --plans 0
$(OUT)/pipe/bank.bin:
	$(MAKE) -f $(SRC)/Makefile pipe
$(OUT)/build_curriculum_bank: $(SRC)/build_curriculum_bank.cpp $(SRC)/curriculum.h $(SRC)/pipe_start.h $(SRC)/rom_check.h $(wildcard ocean/mario_sim/*.h) $(CPU) build/retro/libquicknes.a
	mkdir -p $(OUT)
	clang++ -O2 -std=c++17 -Iocean/mario_sim -Isrc -Iraylib-5.5_linux_amd64/include -Iocean/retro/nes_emu $< $(CPU) build/retro/libquicknes.a raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lm -ldl -lpthread -fopenmp -o $@
curriculum: $(OUT)/build_curriculum_bank $(OUT)/pipe/bank.bin $(MODULE) $(VERIFY) $(OUT)/build_table
	$(OUT)/build_curriculum_bank $(OUT)/curriculum $(OUT)/pipe
	$(VERIFY) $(OUT)/curriculum/validation/clips.bin > $(OUT)/curriculum/cuda_validation.json
	$(OUT)/build_table
$(OUT)/curriculum/bank.bin:
	$(MAKE) -f $(SRC)/Makefile curriculum
$(OUT)/test_cpu: $(SRC)/test_cpu.cpp $(wildcard $(SRC)/*.h) $(wildcard ocean/mario_sim/*.h) $(CPU)
	mkdir -p $(OUT)
	clang++ -O2 -std=c++17 -Isrc -Iraylib-5.5_linux_amd64/include $< $(CPU) -o $@
$(OUT)/test_cuda: $(SRC)/test_cuda.cu $(SRC)/mario_fpg_time.cu $(wildcard $(SRC)/*.h) $(wildcard ocean/mario_sim/*.h) $(CPU) $(MODULE)
	mkdir -p $(OUT)
	$(NVCC) -O2 -arch=sm_61 -std=c++17 -Isrc -Iraylib-5.5_linux_amd64/include $< $(CPU) -L/usr/local/cuda/lib64/stubs -lcuda -o $@
test: table $(OUT)/test_cpu $(OUT)/test_cuda
	$(OUT)/test_cpu
	$(OUT)/test_cuda
$(OUT)/test_curriculum_cpu: $(SRC)/test_curriculum_cpu.cpp $(wildcard $(SRC)/*.h) $(wildcard ocean/mario_sim/*.h) $(CPU)
	clang++ -O2 -std=c++17 -Isrc -Iraylib-5.5_linux_amd64/include $< $(CPU) -o $@
$(OUT)/test_curriculum_cuda: $(SRC)/test_curriculum_cuda.cu $(SRC)/mario_fpg_time.cu $(wildcard $(SRC)/*.h) $(wildcard ocean/mario_sim/*.h) $(CPU) $(MODULE)
	$(NVCC) -O2 -arch=sm_61 -std=c++17 -Isrc -Iraylib-5.5_linux_amd64/include $< $(CPU) -L/usr/local/cuda/lib64/stubs -lcuda -o $@
curriculum-test: table $(OUT)/test_curriculum_cpu $(OUT)/test_curriculum_cuda
	$(OUT)/test_curriculum_cpu
	$(OUT)/test_curriculum_cuda
$(OUT)/policy.o: $(SRC)/policy.c $(SRC)/policy.h $(SRC)/encoder_cpu.h $(SRC)/policy_shape.h src/puffercpu.c
	mkdir -p $(OUT)
	clang -O3 -std=c11 -c $< -o $@
$(OUT)/sim2rom: $(SRC)/sim2rom.cpp $(SRC)/rom_check.h $(SRC)/pipe_start.h $(SRC)/curriculum.h $(SRC)/policy.h $(SRC)/table.h $(SRC)/observation.h $(SRC)/policy_shape.h $(OUT)/policy.o $(CPU) build/retro/libquicknes.a
	clang++ -O2 -std=c++17 -Iocean/mario_sim -Isrc -Iraylib-5.5_linux_amd64/include -Iocean/retro/nes_emu $< $(OUT)/policy.o $(CPU) build/retro/libquicknes.a raylib-5.5_linux_amd64/lib/libraylib.a -lGL -lm -ldl -lpthread -fopenmp -o $@
transfer: $(OUT)/sim2rom

$(OUT)/test_observation: $(SRC)/test_observation.cpp $(SRC)/observation.h $(SRC)/policy_shape.h
	clang++ -O2 -std=c++17 $< -o $@
$(OUT)/test_encoder $(OUT)/test_encoder_bf16: $(SRC)/test_encoder.cu $(SRC)/encoder.cu $(SRC)/encoder_cpu.h $(SRC)/observation.h $(SRC)/policy_shape.h $(OUT)/policy.o $(CPU) $(wildcard src/*.cu src/*.h)
	$(NVCC) -O2 --threads 2 -arch=sm_61 -std=c++17 -I. -Isrc -Ivendor -Iraylib-5.5_linux_amd64/include \
		$(if $(findstring bf16,$@),-DFPT_TEST_BF16) \
		-Xcompiler=-fopenmp,-Wno-narrowing,-Wno-multichar --diag-suppress 550 --diag-suppress 68 --diag-suppress 2361 --diag-suppress 1422 \
		$< $(OUT)/policy.o $(CPU) raylib-5.5_linux_amd64/lib/libraylib.a -L/usr/local/cuda/lib64/stubs \
		-lcuda -lcudart -lcublas -lcusolver -lcurand -lnccl -lnvidia-ml -lgomp -lGL -lm -ldl -lpthread -o $@
semantic-test: $(OUT)/test_observation $(OUT)/test_encoder $(OUT)/test_encoder_bf16
	$(OUT)/test_observation build/mario_sim/qualification_v2/main/clips.bin build/mario_sim/qualification_v2/supplement/clips.bin
	$(OUT)/test_encoder
	$(OUT)/test_encoder_bf16
