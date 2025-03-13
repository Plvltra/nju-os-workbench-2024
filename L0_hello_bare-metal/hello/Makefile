NAME := hello
SRCS := hello.c
export ARCH := x86_64-qemu# x86-qemu x86_64-qemu
export AM_HOME := $(PWD)/../abstract-machine
include $(AM_HOME)/Makefile

debug:
	qemu-system-i386 -S -s -serial none -nographic -machine accel=tcg -smp "1,sockets=1" \
		-drive format=raw,file=./build/hello-$(ARCH) & \
	gdb -x debug.py

log:
	make -nB \
		| grep -ve '^\(\#\|echo\|mkdir\|make\)' \
		| sed "s#$(AM_HOME)#\AM#g" \
		| sed "s#$(PWD)#.#g" \
		> compile.log
# replace space in compile.log with \n

k:
	kill -9 $(pidof qemu-system-i386)

port:
	sudo lsof -i :1234

.PHONY: debug log k port

# NAME := hello
# SRCS := main.c say.c
# # include $(AM_HOME)/Makefile.app
# export MODULE  := L0
# export AM_HOME := $(PWD)/../abstract-machine
# ifeq ($(ARCH),)
# export ARCH := x86_64-qemu
# endif
# # include $(AM_HOME)/Makefile.app
# include $(AM_HOME)/Makefile
# qemu_pid = $(pidof qemu-system-i386) && kill -9 $(qemu_pid)
