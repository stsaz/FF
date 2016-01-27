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
OPT := -O2

override CFLAGS += -c -g -D_DEBUG $(OPT) -Werror -Wall -pthread \
	-I$(FF) -I$(FF)-3pt -I$(FFOS)

override FF_CFLAGS += -g -D_DEBUG $(OPT)

override LDFLAGS += -pthread

ifeq ($(OS),win)
override LDFLAGS += -lws2_32
endif

all: ff $(FF_TEST_BIN)

clean:
	rm -vf $(FF_TEST_BIN) \
		$(FF_TEST_OBJ) $(FF_OBJ) $(FFOS_OBJ)

include $(FF)/makerules


# test
FF_TEST_HDR := $(wildcard $(FF)/test/*.h)

FF_TEST_SRC := \
	$(FF)/test/base.c $(FF)/test/conf.c $(FF)/test/http.c $(FF)/test/json.c $(FF)/test/str.c \
	$(FF)/test/test.c $(FF)/test/time.c $(FF)/test/url.c $(FF)/test/cue.c
FF_TEST_OBJ := $(addprefix $(FF_OBJ_DIR)/, $(addsuffix .o, $(notdir $(basename $(FF_TEST_SRC)))))

$(FF_OBJ_DIR)/%.o: $(FF)/test/%.c $(FF_HDR) $(FF_TEST_HDR)
	$(C) $(CFLAGS)  $< -o$@

FF_TEST_O := $(FFOS_OBJ) $(FF_OBJ) \
	$(FF_OBJ_DIR)/ffhttp.o $(FF_OBJ_DIR)/ffurl.o $(FF_OBJ_DIR)/ffdns.o \
	$(FF_DATA_OBJ) $(FF_OBJ_DIR)/fftest.o $(FF_TEST_OBJ)

$(FF_TEST_BIN): $(FF_TEST_O)
	$(LD) $(FF_TEST_O) $(LDFLAGS) $(LIBS) -o$@
