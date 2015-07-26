# FF v1.3 makefile for GNU make and GCC

FF_TEST_BIN = fftest
OSTYPE = unix
OS = linux

FFOS = ../ffos
FF = .

C = gcc
LD = gcc
O = -o
O_LD = -o

DBG = -g

override CFLAGS += -c $(DBG) -Wall -Werror -I$(FF) -I$(FFOS) -pthread
override LDFLAGS += -lrt -pthread


all: ff $(FF_TEST_BIN)

clean:
	rm -vf $(FF_TEST_BIN) \
		$(FF_TEST_OBJ) $(FF_OBJ) $(FFOS_OBJ)

include $(FF)/makerules
