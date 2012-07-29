# COPYRIGHT_CHUNFENG
include config.mk
OS ?= LINUX32
CC ?= gcc
ifeq ($(OS),WIN32)
CFLAGS += -D_WIN32_
else
CFLAGS += 
endif
ifeq ($(CONFIG_DEBUG),y)
CFLAGS += -g
LDFLAGS += -g
else
CFLAGS += -s
LDFLAGS += -s -O2
endif
VER = $(shell cat VERSION)

#####################################################
GLIB_CFLAGS ?= $(shell pkg-config --cflags glib-2.0)
GLIB_LIBS ?= $(shell pkg-config --libs glib-2.0)

LIBSOUP_CFLAGS ?= $(shell pkg-config --cflags libsoup-2.4)
LIBSOUP_LIBS ?= $(shell pkg-config --libs libsoup-2.4)

JSON_GLIB_CFLAGS ?= $(shell pkg-config --cflags json-glib-1.0)
JSON_GLIB_LIBS ?= $(shell pkg-config --libs json-glib-1.0)

SQLITE3_CFLAGS ?= $(shell pkg-config --cflags sqlite3)
SQLITE3_LIBS ?= $(shell pkg-config --libs sqlite3)

HYBRID_CFLAGS ?= -I/usr/include/hybrid -DENABLE_NLS $(shell pkg-config --cflags gtk+-2.0)

LIBGWEBQQ_CFLAGS ?= -I/usr/include/libgwebqq
LIBGWEBQQ_LIBS ?= -lgwebqq
#####################################################
ifeq ($(OS), WIN32)
TARGET = gwebqq.dll
else
TARGET = libhigwebqq.so
endif

OBJS = hybrid_im_gwebqq.o

CFLAGS +=  -I./include -fPIC $(GLIB_CFLAGS) $(LIBSOUP_CFLAGS) $(JSON_GLIB_CFLAGS) $(SQLITE3_CFLAGS) $(LIBGWEBQQ_CFLAGS) $(HYBRID_CFLAGS)
LDFLAGS += -Wl,-soname -Wl,$(TARGET) -shared -lm -lcrypt $(GLIB_LIBS) $(LIBSOUP_LIBS) $(JSON_GLIB_LIBS) $(SQLITE3_LIBS) $(LIBGWEBQQ_LIBS)
#####################################################

all:$(TARGET)

$(TARGET):$(TARGET).$(VER)
ifeq ($(OS),WIN32)
	mv $^ $@
else
	ln -sf $^ $@
endif

$(TARGET).$(VER):$(OBJS)
	$(CC) $(LDFLAGS) -o$@ $^

_DEST_LIB_DIR = $(DESTDIR)/usr/lib/hybrid/protocols/
install:
	mkdir -p $(_DEST_LIB_DIR)
	cp -dR ${TARGET} ${TARGET}.${VER} $(_DEST_LIB_DIR)
     
uninstall:
	rm -rf $(_DEST_LIB_DIR)/${TARGET} $(_DEST_LIB_DIR)/${TARGET}.$(VER)

clean:
	rm -rf test $(TARGET)  $(TARGET).${VER} *.o

.PHONY : all clean install uninstall

