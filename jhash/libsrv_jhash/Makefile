
all:
	gcc -shared -fPIC -o libsrv_jhash.so libsrv_jhashzero.c #-DDEBUG 
	$(if $(BUILD_DIR), cp libsrv_jhash.so jhash_ker.cl $(BUILD_DIR)/ )

clean:
	rm -f *.o *.so
