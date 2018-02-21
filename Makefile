#CROSS_COMPILE ?= arm-linux
CROSS_COMPILE ?= i586-linux-gnu

CC := $(CROSS_COMPILE)-gcc
AR := $(CROSS_COMPILE)-ar
LD := $(CROSS_COMPILE)-ld
STRIP := $(CROSS_COMPILE)-strip

TOP := $(shell pwd)

LUAJIT_DIR := $(TOP)/3rd/luajit
CJSON_DIR  := $(TOP)/3rd/cJSON
LIBEV_DIR  := $(TOP)/3rd/libev/src
LIBUV_DIR  := $(TOP)/3rd/libuv
RD3ROOT    := $(TOP)/3rd/usr

LINKLIBS := -ldl -lpthread -lm

#DEBUG_FLAG := -O2
DEBUG_FLAG := -O0 -g -ggdb

LINK_ARCHIVES := -Wl,-whole-archive -lcjson -luv -lluajit-5.1 -Wl,-no-whole-archive

CFLAGS  += -I$(TOP)/include -I$(RD3ROOT)/include -std=gnu99 -Wall $(DEBUG_FLAG) \
		   -I$(RD3ROOT)/include/luajit-2.0 -I$(RD3ROOT)/include/cjson -fPIC
LDFLAGS += -L$(TOP) -L$(RD3ROOT)/lib -Wl,--start-group $(LINKLIBS) -Wl,--end-group

OBJS += core/xu_utils.o \
		core/xu_malloc.o \
		core/xu_print.o \
		core/xu_init.o \
		core/xu_env.o \
		core/xu_ctx.o \

all: libxucore.so demo

libxucore.so : $(OBJS)
	$(CC) -shared $(LDFLAGS) $(LINK_ARCHIVES) -o $@ $^

luajit:
	$(MAKE) HOST_CC=gcc STATIC_CC=$(CC) PREFIX=/usr CFLAGS='-fPIC -O2' DYNAMIC_CC='$(CC) -fPIC' TARGET_LD=$(CC) TARGET_AR='$(AR) rcs' TARGET_STRIP=$(STRIP) DESTDIR=$(TOP)/3rd -C $(LUAJIT_DIR) install clean

cJSON:
	(cd $(CJSON_DIR) && git checkout -f && sed -i 's/gcc/arm-linux-gcc/g' $(CJSON_DIR)/Makefile)
	$(MAKE) CC=$(CC) AR=$(AR) LD=$(LD)  PREFIX=$(RD3ROOT) -C $(CJSON_DIR) static shared install
	(cp -f $(CJSON_DIR)/libcjson.a $(CJSON_DIR)/libcjson_utils.a $(RD3ROOT)/lib && cd $(CJSON_DIR) && make clean && git checkout -f Makefile)

libev:
	@(cd $(LIBEV_DIR); [ -f configure ] || sh autogen.sh)
	(cd $(LIBEV_DIR) && ./configure --prefix=$(RD3ROOT) --host=$(CROSS_COMPILE) --disable-shared)
	$(MAKE) -C $(LIBEV_DIR) install distclean
	@rm -rf $(RD3ROOT)/lib/libev.la

libuv:
	@(cd $(LIBUV_DIR); [ -f configure ] || sh autogen.sh)
	(cd $(LIBUV_DIR) && ./configure --prefix=$(RD3ROOT) --host=$(CROSS_COMPILE) --disable-shared)
	$(MAKE) -C $(LIBUV_DIR) install distclean
	@rm -rf $(RD3ROOT)/lib/*.la

dependence: cJSON luajit libuv
	@rm -rf $(RD3ROOT)/lib/*.so
	@rm -rf $(RD3ROOT)/lib/*.la
	@rm -rf $(RD3ROOT)/lib/*.so.*
	@echo "done"

luaclib : luaclib/cjson.so
	@echo "build all luaclibs"

luaclib/cjson.so: luaclib/cjson.o
	$(CC) $(CFLAGS) $(LDFLAGS) -fPIC -shared $^ -o $@ 

demo: tests/xc.o libxucore.so
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS) -Wl,-rpath,. -static-libgcc

actor:
	$(MAKE) CC=$(CC) CFLAGS="$(CFLAGS)" -C $(TOP)/svc

clean:
	rm -rf *.o $(OBJS) tests/*.o svc/*.o

distclean: clean
	rm -rf *.a $(RD3ROOT) *.so svc/*.so demo
