# FF makefile

ROOT := ..
FFOS = $(ROOT)/ffos
FF = $(ROOT)/ff
FF3PT := $(ROOT)/ff-3pt
DEBUG := 1
OPT := 3

include $(FFOS)/makeconf

ifeq ($(OS),win)
FF_TEST_BIN := fftest.exe
else
FF_TEST_BIN := fftest
endif

FFOS_CFLAGS := $(CFLAGS)
FF_CFLAGS := $(CFLAGS) -DFFHST_DEBUG
CFLAGS += -Werror -Wall -DFF_NO_OBSOLETE -DFFHST_DEBUG \
	-I$(FF) -I$(FF3PT) -I$(FFOS)
CXXFLAGS += -Werror -Wall \
	-I$(FF) -I$(FF3PT) -I$(FFOS)

all: ff $(FF_TEST_BIN)

clean:
	rm -vf $(FF_TEST_BIN) \
		$(FF_TEST_OBJ) $(FF_OBJ) $(FFOS_OBJ)

include $(FF)/makerules
include $(FF3PT)/makerules


# test
FF_TEST_HDR := $(wildcard $(FF)/test/*.h)

FF_TEST_SRC := \
	$(FF)/test/base.c $(FF)/test/conf.c $(FF)/test/http.c $(FF)/test/json.c $(FF)/test/str.c \
	$(FF)/test/test.c $(FF)/test/time.c $(FF)/test/url.c $(FF)/test/cue.c $(FF)/test/sys.c \
	$(FF)/test/arc.c \
	$(FF)/test/tls.c \
	$(FF)/test/webskt.c \
	$(FF)/test/hashtab.c
# $(FF)/test/compat.cpp
FF_TEST_OBJ := $(addprefix $(FF_OBJ_DIR)/, $(addsuffix .o, $(notdir $(basename $(FF_TEST_SRC)))))
FF_TEST_OBJ += $(FF_OBJ_DIR)/sha1.o $(FF_OBJ_DIR)/base64.o

$(FF_OBJ_DIR)/%.o: $(FF)/test/%.c $(FF_HDR) $(FF_TEST_HDR)
	$(C) $(CFLAGS)  $< -o$@
$(FF_OBJ_DIR)/%.o: $(FF)/test/%.cpp $(FF_HDR) $(FF_TEST_HDR)
	$(CXX) $(CXXFLAGS)  $< -o$@

FF_TEST_O := $(FFOS_OBJ) $(FF_OBJ) \
	$(FFOS_THD) \
	$(FFOS_SKT) \
	$(FF_OBJ_DIR)/ffdbg.o \
	$(FF_OBJ_DIR)/fftmr.o \
	$(FF_OBJ_DIR)/ffhttp.o $(FF_OBJ_DIR)/ffproto.o $(FF_OBJ_DIR)/ffurl.o $(FF_OBJ_DIR)/ffdns.o \
	$(FF_OBJ_DIR)/fficy.o \
	$(FF_OBJ_DIR)/ffconf.o \
	$(FF_OBJ_DIR)/ffjson.o \
	$(FF_OBJ_DIR)/ffparse.o \
	$(FF_OBJ_DIR)/ffpsarg.o \
	$(FF_OBJ_DIR)/ffutf8.o \
	$(FF_OBJ_DIR)/ffcue.o \
	$(FF_OBJ_DIR)/ffxml.o \
	$(FF_OBJ_DIR)/ffsendfile.o \
	$(FF_OBJ_DIR)/ffiso.o $(FF_OBJ_DIR)/ffiso-fmt.o \
	$(FF_OBJ_DIR)/fftls.o \
	$(FF_OBJ_DIR)/ffwebskt.o \
	$(FF_OBJ_DIR)/fftest.o $(FF_TEST_OBJ)

$(FF_TEST_BIN): $(FF_TEST_O)
	$(LD) $(FF_TEST_O) $(LDFLAGS) $(LIBS) $(LD_LWS2_32) $(LD_LPTHREAD) -o$@


FF_TESTSSL_O := $(FFOS_OBJ) $(FF_OBJ) \
	$(FF_OBJ_DIR)/ffdbg.o \
	$(FF_OBJ_DIR)/ffutf8.o \
	$(FF_OBJ_DIR)/ffparse.o \
	$(FF_OBJ_DIR)/ffssl.o \
	$(FF_OBJ_DIR)/fftest.o \
	$(FF_OBJ_DIR)/ssl.o
fftest-ssl: $(FF_TESTSSL_O)
	$(LD) $(FF_TESTSSL_O) $(LDFLAGS) -L$(FF3PT)-bin/$(OS)-$(ARCH) -lcrypto -lssl $(LD_LDL)  -o$@
