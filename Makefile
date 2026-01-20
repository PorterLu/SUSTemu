.PHONY: all run clean menuconfig

PWD = $(shell pwd)
OBJ_DIR = $(PWD)/build
TARGET = $(OBJ_DIR)/sustemu

ASRCS = $(shell find ./src -name "*.S")
CSRCS = $(shell find ./src -name "*.c")
CXXSRCS = $(shell find ./src -name "*.cc")
SRCS = $(ASRCS) $(CSRCS) $(CXXSRCS)

OBJS = $(CSRCS:%.c=$(OBJ_DIR)/%.o) $(CXXSRCS:%.cc=$(OBJ_DIR)/%.o) $(ASRCS:%.S=$(OBJ_DIR)/%.o)

INC_PATH := $(abspath $(shell find ./include -maxdepth 1))
INC_PATH := $(filter-out  $(abspath ./include/config), $(INC_PATH))
INC_PATH += $(abspath ./include/generated)

CXX = g++
CC = gcc
LD = g++
AS = gcc

INC_PATH := $(addprefix -I,$(INC_PATH))

LOG_FILE = log.txt 
IMG_FILE = os_test/os.elf 
IMG_BIN = os_test/os.bin

LIBS = -lSDL2 -lreadline
CFLAGS = -O2 -Wall -Werror -MMD $(INC_PATH) 
CXXFLAGS = $(shell llvm-config-11 --cxxflags) 
CXXFLAGS +=  $(shell llvm-config-11 --libs) 
CXXFLAGS += -std=c++14  -fno-exceptions -fPIE
CXXFLAGS += $(CFLAGS)
ASFLAGS = -MMD -O0 $(INC_PATH)
LDFLAGS = -O2 $(LIBS) -lLLVM-11

all: $(TARGET)
	@echo $< over

test: all 
	make -C ./test
	./build/sustemu -e ./test/kernel.elf -l log.txt ./test/kernel.bin

$(TARGET): $(OBJS)
	@$(LD) -o $@ $(OBJS) $(LDFLAGS) $(LIBS) 

$(OBJ_DIR)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LIBS) -c -o $@ $<

$(OBJ_DIR)/%.o: %.cc
	mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(OBJ_DIR)/%.o: %.S 
	mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -c -o $@ $<

run: $(TARGET)
	$(TARGET) -l $(LOG_FILE) -e $(IMG_FILE) $(IMG_BIN)

clean:
	rm $(OBJ_DIR) -rf
	find ./ -name "*.o" | xargs rm -rf
	find ./ -name "*.d" | xargs rm -rf
	rm $(LOG_FILE) -rf
	make -C ./test clean

menuconfig:
	mkdir -p ./build
	mconf Kconfig
	conf --syncconfig Kconfig

-include $(CSRCS:%.c=%.d) $(CXXSRCS:%.cc=%.d) $(ASRCS:%.S=%.d)
