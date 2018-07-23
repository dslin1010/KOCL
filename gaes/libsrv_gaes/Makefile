
all: 
	gcc -shared -fPIC -o libsrv_gaes.so srv_gaeszero.c #-DDEBUG 
	$(if $(BUILD_DIR), cp libsrv_gaes.so gaes.cl $(BUILD_DIR)/ ) 

clean:
	rm -f *.o *.so
