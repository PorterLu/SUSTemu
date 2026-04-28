.PHONY: all run clean menuconfig bench bench-functional bench-inorder bench-bpred bench-ooo bench-dhrystone bench-dual run-mario run-mario-functional run-mario-inorder run-mario-difftest

PWD = $(shell pwd)
OBJ_DIR = $(PWD)/build
TARGET = $(OBJ_DIR)/sustemu

ASRCS = $(shell find ./src -name "*.S")
CSRCS = $(shell find ./src -name "*.c")
CXXSRCS = $(shell find ./src -name "*.cc")
SRCS = $(ASRCS) $(CSRCS) $(CXXSRCS)

OBJS = $(CSRCS:%.c=$(OBJ_DIR)/%.o) $(CXXSRCS:%.cc=$(OBJ_DIR)/%.o) $(ASRCS:%.S=$(OBJ_DIR)/%.o)

INC_PATH := $(abspath $(shell find ./include -maxdepth 1 -type d))
INC_PATH := $(filter-out  $(abspath ./include/config), $(INC_PATH))
INC_PATH += $(abspath ./include/generated)

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  CXX = clang++
  CC = clang
  LD = clang++
  AS = clang
  LLVM_CONFIG = /opt/homebrew/opt/llvm/bin/llvm-config
  BREW_SDL2 = /opt/homebrew/opt/sdl2
  BREW_RL   = /opt/homebrew/opt/readline

  LIBS = -lSDL2 -lreadline
  CFLAGS = -O3 -march=native -flto -Wall -Wno-format -MMD $(INC_PATH)
  CFLAGS += -I$(BREW_SDL2)/include -I$(BREW_RL)/include
  CXXFLAGS = $(shell $(LLVM_CONFIG) --cxxflags)
  CXXFLAGS += -fno-exceptions -fPIE
  CXXFLAGS += $(CFLAGS)
  ASFLAGS = -MMD -O0 $(INC_PATH)
  LDFLAGS = -O2 -flto -L$(BREW_SDL2)/lib -L$(BREW_RL)/lib $(LIBS)
  LDFLAGS += $(shell $(LLVM_CONFIG) --ldflags)
  LDFLAGS += $(shell $(LLVM_CONFIG) --libs)
else
  CXX = g++
  CC = gcc
  LD = g++
  AS = gcc

  LIBS = -lSDL2 -lreadline
  CFLAGS = -O3 -march=native -Wall -MMD $(INC_PATH)
  CXXFLAGS = $(shell llvm-config-11 --cxxflags)
  CXXFLAGS += $(shell llvm-config-11 --libs)
  CXXFLAGS += -std=c++14 -fno-exceptions -fPIE
  CXXFLAGS += $(CFLAGS)
  ASFLAGS = -MMD -O0 $(INC_PATH)
  LDFLAGS = -O2 $(LIBS) -lLLVM-11
endif

INC_PATH := $(addprefix -I,$(INC_PATH))

LOG_FILE = log.txt
IMG_FILE = os_test/os.elf
IMG_BIN = os_test/os.bin

all: $(TARGET)
	@echo $< over

test: all 
	make -C ./test
	./build/sustemu -b -e ./test/kernel.elf -l log.txt ./test/kernel.bin

bench: all
	make -C ./test
	@echo "=== Functional mode ==="
	./build/sustemu -b -e ./test/kernel.elf ./test/kernel.bin
	@echo "=== In-order pipeline (no branch predictor) ==="
	./build/sustemu -b --inorder -e ./test/kernel.elf ./test/kernel.bin
	@echo "=== In-order pipeline + BTB/Tournament branch predictor ==="
	./build/sustemu -b --inorder --bpred -e ./test/kernel.elf ./test/kernel.bin
	@echo "=== OOO engine (Tomasulo + ROB) + BTB/Tournament branch predictor ==="
	./build/sustemu -b --ooo --bpred -e ./test/kernel.elf ./test/kernel.bin

bench-functional: all
	make -C ./test
	./build/sustemu -b -e ./test/kernel.elf ./test/kernel.bin

bench-inorder: all
	make -C ./test
	./build/sustemu -b --inorder -e ./test/kernel.elf ./test/kernel.bin

bench-bpred: all
	make -C ./test
	./build/sustemu -b --inorder --bpred -e ./test/kernel.elf ./test/kernel.bin

bench-ooo: all
	make -C ./test/dhrystone
	@echo "=== OOO engine (Tomasulo + ROB) + BTB/Tournament branch predictor — Dhrystone ==="
	./build/sustemu -b --ooo --bpred -e ./test/dhrystone/dhrystone.elf -l /dev/null ./test/dhrystone/dhrystone.bin

bench-dhrystone: all
	make -C ./test/dhrystone
	@echo "=== Dhrystone 2.1 — Functional mode ==="
	./build/sustemu -b -e ./test/dhrystone/dhrystone.elf -l /dev/null ./test/dhrystone/dhrystone.bin
	@echo "=== Dhrystone 2.1 — In-order pipeline + bpred ==="
	./build/sustemu -b --inorder --bpred -e ./test/dhrystone/dhrystone.elf -l /dev/null ./test/dhrystone/dhrystone.bin
	@echo "=== Dhrystone 2.1 — OOO engine + bpred ==="
	./build/sustemu -b --ooo --bpred -e ./test/dhrystone/dhrystone.elf -l /dev/null ./test/dhrystone/dhrystone.bin

bench-dual: all
	make -C ./test/dual
	@echo "=== Dual-core OOO + bpred — each hart runs independent Dhrystone ==="
	./build/sustemu -b --ooo --bpred --dual -e ./test/dual/dual.elf -l /dev/null ./test/dual/dual.bin

AM_HOME    = $(PWD)/am
LITENES_DIR = $(PWD)/litenes
LITENES_ELF = $(LITENES_DIR)/build/litenes-riscv64-nemu.elf
LITENES_BIN = $(LITENES_DIR)/build/litenes-riscv64-nemu.bin

$(LITENES_ELF): $(LITENES_BIN)
$(LITENES_BIN):
	AM_HOME=$(AM_HOME) make -C $(LITENES_DIR) ARCH=riscv64-nemu

ifeq ($(UNAME_S),Linux)
  TASKSET = taskset -c 0
else
  TASKSET =
endif

run-mario-functional: all $(LITENES_BIN)
	DISPLAY=:0 $(TASKSET) ./build/sustemu -b -e $(LITENES_ELF) $(LITENES_BIN)

run-mario: all $(LITENES_BIN)
	DISPLAY=:0 $(TASKSET) ./build/sustemu --ooo --bpred -b -e $(LITENES_ELF) $(LITENES_BIN)

run-mario-inorder: all $(LITENES_BIN)
	DISPLAY=:0 $(TASKSET) ./build/sustemu --inorder --bpred -b -e $(LITENES_ELF) $(LITENES_BIN)

run-mario-difftest: all $(LITENES_BIN)
	DISPLAY=:0 ./build/sustemu --ooo --bpred --difftest -b -e $(LITENES_ELF) $(LITENES_BIN)
$(TARGET): $(OBJS)
	@$(LD) -o $@ $(OBJS) $(LDFLAGS) 

$(OBJ_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: %.cc
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: %.S 
	mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c -o $@ $<

run: $(TARGET)
	$(TARGET) -l $(LOG_FILE) -e $(IMG_FILE) $(IMG_BIN)

clean:
	rm -rf $(OBJ_DIR)
	find ./ -name "*.o" -delete
	find ./ -name "*.d" -delete
	rm -rf $(LOG_FILE)
	make -C ./test clean 2>/dev/null || true

menuconfig:
	mkdir -p ./build
ifeq ($(UNAME_S),Darwin)
	menuconfig Kconfig
	python3 -c "from kconfiglib import Kconfig; k=Kconfig('Kconfig'); k.load_config('.config'); k.write_autoconf('include/generated/autoconf.h')"
else
	mconf Kconfig
	conf --syncconfig Kconfig
endif

-include $(CSRCS:%.c=%.d) $(CXXSRCS:%.cc=%.d) $(ASRCS:%.S=%.d)
