# FF makefile

ROOT := ../..
FFOS = $(ROOT)/ffos
FFBASE := $(ROOT)/ffbase
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

# CFLAGS += -DFF_WIN=0x0501
FFOS_CFLAGS := $(CFLAGS) -Werror
FF_CFLAGS := $(CFLAGS) -DFFHST_DEBUG -DFFBASE_HAVE_FFERR_STR -Werror
CFLAGS += -Werror -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
	-DFFBASE_HAVE_FFERR_STR -DFF_NO_OBSOLETE -DFFHST_DEBUG \
	-I$(FFBASE) -I$(FF) -I$(FF3PT) -I$(FFOS)
CXXFLAGS += -Werror -Wall \
	-I$(FFBASE) -I$(FF) -I$(FF3PT) -I$(FFOS)
# CFLAGS += -fsanitize=address
# LDFLAGS += -fsanitize=address

all: ff $(FF_TEST_BIN)

clean:
	rm -vf $(FF_TEST_BIN) \
		$(FF_TEST_OBJ) $(FF_TEST_O) $(FF_OBJ) \
		$(FFOS_OBJ) $(FFOS_SKT) $(FFOS_THD)

include $(FF)/makerules
include $(FF3PT)/makerules


# test
FF_TEST_HDR := $(wildcard $(FF)/test/*.h)

FF_TEST_SRC := \
	$(wildcard $(FF)/test/base-*.c) \
	$(FF)/test/base.c \
	$(FF)/test/test.c $(FF)/test/time.c $(FF)/test/sys.c \
	$(FF)/test/path.c \
	$(wildcard $(FF)/test/pack-*.c) \
	$(wildcard $(FF)/test/net-*.c) \
	$(wildcard $(FF)/test/data-*.c) \
	$(FF)/test/cache.c \
	$(FF)/test/compat.cpp
FF_TEST_OBJ := $(addprefix ./, $(addsuffix .o, $(notdir $(basename $(FF_TEST_SRC)))))
FF_TEST_OBJ += $(FF_OBJ_DIR)/sha1.o $(FF_OBJ_DIR)/base64.o

./%.o: $(FF)/test/%.c $(FF_HDR) $(FF_TEST_HDR) $(FF)/test/Makefile
	$(C) $(CFLAGS)  $< -o$@
./%.o: $(FF)/test/%.cpp $(FF_HDR) $(FF_TEST_HDR) $(FF)/test/Makefile
	$(CXX) $(CXXFLAGS)  $< -o$@

FF_TEST_O := $(FFOS_OBJ) $(FF_OBJ) \
	$(FFOS_THD) \
	$(FFOS_SKT) \
	$(FF_OBJ_DIR)/fftmr.o \
	$(FF_OBJ_DIR)/ffhttp.o $(FF_OBJ_DIR)/ffproto.o $(FF_OBJ_DIR)/ffurl.o \
	$(FF_OBJ_DIR)/fficy.o \
	$(FF_OBJ_DIR)/ffconf.o \
	$(FF_OBJ_DIR)/ffjson.o \
	$(FF_OBJ_DIR)/ffparse.o \
	$(FF_OBJ_DIR)/ffpsarg.o \
	$(FF_OBJ_DIR)/ffutf8.o \
	$(FF_OBJ_DIR)/ffcue.o \
	$(FF_OBJ_DIR)/ffxml.o \
	$(FF_OBJ_DIR)/ffdns-client.o \
	$(FF_OBJ_DIR)/ffcache.o \
	$(FF_OBJ_DIR)/ffsendfile.o \
	$(FF_OBJ_DIR)/fffileread.o \
	$(FF_OBJ_DIR)/fffilewrite.o \
	$(FF_OBJ_DIR)/ffthpool.o \
	$(FF_OBJ_DIR)/fftls.o \
	$(FF_OBJ_DIR)/ffwebskt.o \
	$(FF_TEST_OBJ)

$(FF_TEST_BIN): $(FF_TEST_O)
	$(LD) $(FF_TEST_O) $(LDFLAGS) $(LIBS) $(LD_LWS2_32) $(LD_LPTHREAD) -o$@

FF_TESTSSL_O := $(FFOS_OBJ) $(FF_OBJ) \
	$(FF_OBJ_DIR)/ffutf8.o \
	$(FF_OBJ_DIR)/ffparse.o \
	$(FF_OBJ_DIR)/ffssl.o \
	./ssl.o
fftest-ssl: $(FF_TESTSSL_O)
	$(LD) $(FF_TESTSSL_O) $(LDFLAGS) -L$(FF3PT)-bin/$(OS)-$(ARCH) -lcrypto -lssl $(LD_LDL)  -o$@

FF_TEST_SQLITE_O := $(FFOS_OBJ) $(FF_OBJ) \
	$(FF_OBJ_DIR)/ffutf8.o \
	$(FF_OBJ_DIR)/ffparse.o \
	./db-sqlite.o
fftest-sqlite: ff-obj $(FF_TEST_SQLITE_O)
	$(LD) $(FF_TEST_SQLITE_O) $(LDFLAGS) -L$(FF3PT)-bin/$(OS)-$(ARCH) -lsqlite3-ff  -o$@

FF_TEST_PGSQL_O := $(FFOS_OBJ) $(FF_OBJ) \
	$(FF_OBJ_DIR)/ffutf8.o \
	$(FF_OBJ_DIR)/ffparse.o \
	$(FF_OBJ_DIR)/ffdb-postgre.o \
	./db-postgre.o
fftest-postgre: ff-obj $(FF_TEST_PGSQL_O)
	$(LD) $(FF_TEST_PGSQL_O) $(LDFLAGS) -L$(FF3PT)-bin/$(OS)-$(ARCH) -lpq  -o$@

FF_TEST_AES_O := $(FFOS_OBJ) $(FF_OBJ) \
	./aes.o
fftest-aes: ff-obj $(FF_TEST_AES_O)
	$(LD) $(FF_TEST_AES_O) $(LDFLAGS) $(FF3PT)-bin/$(OS)-$(ARCH)/AES-ff.a  -o$@
