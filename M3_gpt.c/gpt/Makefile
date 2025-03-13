NAME := $(shell basename $(PWD))
# NAME := test
export MODULE := M3
all: $(NAME)-64 $(NAME)-32
LDFLAGS := -lm -lpthread

include ../Makefile
