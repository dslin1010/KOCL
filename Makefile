SUBDIRS = kocl jhash gaes
all: $(SUBDIRS)


.PHONY: $(SUBDIRS)

$(SUBDIRS): mkbuilddir
	$(MAKE) -C $@ $(TARGET) kv=$(kv) BUILD_DIR=`pwd`/build

mkbuilddir:
	mkdir -p build

services: kocl

distclean:
	$(MAKE) all kv=$(kv) TARGET=clean

clean: distclean
	rm -rf build
