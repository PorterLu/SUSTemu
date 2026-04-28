NAME = litenes
SRCS = $(shell find src/ -name "*.c")
include $(AM_HOME)/Makefile
# Last -O flag wins: override AM's -Os with -O3 for better guest performance
CFLAGS += -O3
