NAME = litenes
SRCS = $(shell find src/ -name "*.c")
include $(AM_HOME)/Makefile
# Last -O flag wins: override AM's -Os with -O3 for better guest performance
# -flto removed: AM uses bare ld which lacks LTO plugin support.
# Cross-TU inlining is handled by static-inline in common.h instead.
CFLAGS += -O3
