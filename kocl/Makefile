obj-m += kocl.o

all:	kocl helper

kocl-objs := main.o kocl_log.o

kocl:
	make -C /lib/modules/$(shell uname -r)/build M=`pwd` modules
	$(if $(BUILD_DIR), cp kocl.ko $(BUILD_DIR)/ ) 

kocl_log:
	gcc -O2 -D__KOCL__ -c kocl_log.c -o kocl_log_user.o 
	ar -rcs kocl_log.a kocl_log_user.o
	$(if $(BUILD_DIR), cp kocl_log.a $(BUILD_DIR)/ )

helper: kocl_log
	gcc -O2 -D__KOCL__ -c helper.c 
	gcc -O2 -D__KOCL__ -c service.c 
	gcc -O2 -D__KOCL__ -c gpuops.c
	gcc -O2 -D__KOCL__ -g -Wall service.o helper.o kocl_log_user.o gpuops.o -o helper -lOpenCL -ldl -L /opt/intel/opencl/
	$(if $(BUILD_DIR), cp helper $(BUILD_DIR)/ )

clean:
	make -C /lib/modules/$(shell uname -r)/build M=`pwd` clean
	rm -f helper
	rm -f kocl_log.a
	rm -f *.o
