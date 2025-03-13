NAME := $(shell basename $(PWD))
export MODULE := M4
all: $(NAME)-64 $(NAME)-32
LDFLAGS += -ldl -lrt

include ../Makefile

.ONESHELL:
run:
	export LD_LIBRARY_PATH=/tmp:$LD_LIBRARY_PATH
	./$(NAME)-64