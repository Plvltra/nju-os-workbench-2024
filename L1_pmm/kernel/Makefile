NAME           := kernel
SRCS           := framework/main.c $(shell find -L ./src/ -name "*.c")
INC_PATH       := include/ framework/

ifeq ($(ARCH),)
export ARCH    := x86_64-qemu
endif

include $(AM_HOME)/Makefile
include ../oslabs.mk
image: git

# 非am的测试环境, 无spinlock和am的组件
test: 
	@gcc -g src/pmm.c \
		 $(shell find test/ -name "*.c") \
		 -Iinclude -Iframework -Itest -DTEST -lpthread \
		 -o build/test
	@build/test 0
	@build/test 1
	@build/test 2

# am环境
# make -B run
