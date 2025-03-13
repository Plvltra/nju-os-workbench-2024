NAME           := kernel
SRCS           := framework/main.c $(shell find -L ./src/ -name "*.c")
INC_PATH       := include/ framework/
CFLAGS         += -DSIMPLE_PMM
# CFLAGS         += -DTRACE_F
smp             = 2
CFLAGS         += -DDEBUG_PRODUCER_CONSUMER
# CFLAGS         += -DDEBUG_TTY


ifeq ($(AM_HOME),)
export AM_HOME := $(PWD)/../abstract-machine
endif
ifeq ($(ARCH),)
export ARCH    := x86_64-qemu
# export ARCH    := x86-qemu
endif

include $(AM_HOME)/Makefile
include ../oslabs.mk
# image: git

# qemu-system-x86_64 -s -S build/kernel-x86_64-qemu &
# qemu-system-x86_64 -s -S -serial mon:stdio -machine accel=tcg -smp "cores=1,sockets=1" -drive format=raw,file=/home/zzx/os-workbench-2024/kernel/build/kernel-x86_64-qemu &

start: image Makefile
	@qemu-system-x86_64 -s -S $(QEMU_FLAGS) &

debug: image Makefile
	@qemu-system-x86_64 -s -S $(QEMU_FLAGS) &
	gdb -tui -x debug.py

# 测试用例
# 1. smp=1时生产者消费者
# 2. smp=2时生产者消费者
# 3. smp超过线程数量时生产者消费者
# 4. smp=1时tty-reader
# 5.
# 6.