obj-m += callgpu_async_jhash.o #callgpu_async_jhash_multidev.o 
ccflags-y := -std=gnu99 -Wno-declaration-after-statement

all:
	cp ../../kocl/Module.symvers ./
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) modules
	$(if $(BUILD_DIR), cp callgpu_async_jhash.ko $(BUILD_DIR)/ )

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(shell pwd) clean


