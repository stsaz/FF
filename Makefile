# FF v1.3 makefile for GNU make and GCC

FF_TEST_BIN = fftest
OSTYPE = unix
OS = linux

FFOS = ../ffos
FFOS_OBJ_DIR = ./ffos-obj
FF = .
FF_OBJ_DIR = .

C = gcc
LD = gcc
O = -o
O_LD = -o

DBG = -g

override CFLAGS += -c $(DBG) -Wall -Werror -I$(FF) -I$(FFOS) -pthread
override LDFLAGS += -lrt -pthread


all: $(FFOS_OBJ_DIR) $(FF_TEST_BIN)

clean:
	rm -vf $(FF_TEST_BIN) \
		$(FF_TEST_OBJ) $(FF_OBJ) $(FFOS_OBJ)

$(FFOS_OBJ_DIR):
	mkdir $(FFOS_OBJ_DIR)

include $(FFOS)/makerules

include $(FF)/makerules
