SUBDIRS = libsrv_gaes gaes_ecb callaes #ecryptfs_4.7_kocl ecryptfs_4.7_orig

all: $(SUBDIRS)

.PHONY: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@ $(TARGET) BUILD_DIR=$(BUILD_DIR)

clean:
	$(MAKE) all TARGET=clean
