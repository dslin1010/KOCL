/*
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the GPL-COPYING file in the top-level directory.
 *
 * Copyright (c) 2010-2011 University of Utah and the Flux Group.
 * All rights reserved.
 *
 * GPU service management.
 *
 * link with -ldl
 */
#include <string.h>
#include <dlfcn.h>
#include <stdio.h>
#include <glob.h>
#include <stdlib.h>
#include "list.h"
#include "helper.h"
#include "gpuops.h"
#include "service.h"

struct _kocl_sitem {
    struct kocl_service *s;
    void* libhandle;
    struct list_head list;
};

LIST_HEAD(services);//初始化services list的head 讓prev,next指向自己

static struct _kocl_sitem *lookup_kocl_sitem(const char *name)
{
    struct _kocl_sitem *i;
    struct list_head *e;
    
    if (!name)
	return NULL;

    list_for_each(e, &services) {//在service list 裡面找
	i = list_entry(e, struct _kocl_sitem, list);//現在知道e指向serivce list entry位址,輸入sturct type與list member可找出struct的起始位址
	if (!strncmp(name, i->s->name, KOCL_SERVICE_NAME_SIZE))//比對name與list node裡的name 是否一樣
	    return i;
    }

    return NULL;    
}

struct kocl_service *kh_lookup_service(const char *name)
{
    struct _kocl_sitem *i = lookup_kocl_sitem(name);
     if (!i)
	   return NULL;
    return i->s;
}

int kh_register_service(struct kocl_service *s, void *libhandle)
{
    struct _kocl_sitem *i;

    if (!s)
	return 1;
    i = (struct _kocl_sitem *)malloc(sizeof(struct _kocl_sitem));
    if (!i)
	return 1;

    i->s = s;
    i->libhandle = libhandle;
    INIT_LIST_HEAD(&i->list);

    list_add_tail(&i->list, &services);//把i->list head 加入services FIFO Queue list

    return 0;
}

static int __unregister_service(struct _kocl_sitem *i)
{
    if (!i)
	return 1;

    list_del(&i->list);
    free(i);

    return 0;
}

int kh_unregister_service(const char *name)
{
    return __unregister_service(lookup_kocl_sitem(name));    
}

int kh_load_service(const char *libpath)
{
    void *lh;
    fn_init_service init;//宣告一個fn_init_service的function pointer
    CLsetup clsetup;
    char *err;
    int r=1;
       

    lh = dlopen(libpath, RTLD_LAZY);//dlopen(filename,flag)載入動態函式庫的檔名
    if (!lh)
    {
	fprintf(stderr,
		"Warning: open %s error, %s\n",
		libpath, dlerror());
    } else {
	init = (fn_init_service)dlsym(lh, SERVICE_INIT);//找到init_service的funcion assign 給init
    
    clsetup = (CLsetup)dlsym(lh, SERVICE_CL);//找到service_CLsetup的funcion assign 給clsetup
    if(!clsetup){
        fprintf(stderr,
		    "Warning: %s has no service %s\n",
		    libpath, ((err=dlerror()) == NULL?"": err));
	    dlclose(lh);
    }else{
       // printf("service_CLset start  ~\n"); 
        service_CLset(clsetup);
    }

    if (!init){
	    fprintf(stderr,
		    "Warning: %s has no service %s\n",
		    libpath, ((err=dlerror()) == NULL?"": err));
	    dlclose(lh);
	} else {
	    if (init(lh, kh_register_service))//註冊kocl service
	    {
		fprintf(stderr,
			"Warning: %s failed to register service\n",
			libpath);
		dlclose(lh);
	    } else
		r = 0;
	 }     	    
    }
    

    return r;
}

int kh_load_all_services(const char *dir)
{
    char path[256];
    int i;
    char *libpath;
    int e=0;

    glob_t glb = {0,NULL,0};

                            //dir就是./  SERVICE_LIB_PREFIX就是libsrv_ 
    snprintf(path, 256, "%s/%s*", dir, SERVICE_LIB_PREFIX);
    glob(path, 0, NULL, &glb);
  
    for (i=0; i<glb.gl_pathc; i++)
    {
	libpath = glb.gl_pathv[i];
	e += kh_load_service(libpath);//把libsrv_ 的檔案載入
    }

    globfree(&glb);
    return e;
}

static int __unload_service(struct _kocl_sitem* i)
{
    void *lh;
    fn_finit_service finit;
    int r=1;
    if (!i)
	return 1;

    lh = i->libhandle;
    
    if (lh) {
	finit = (fn_finit_service)dlsym(lh, SERVICE_FINIT);
	if (finit)
	{
	    if (finit(lh, kh_unregister_service))
	    {
		fprintf(stderr,
			"Warning: failed to unregister service %s\n",
			i->s->name);
	    } else
		r = 0;
	} else {
	    __unregister_service(i);
	    r = 0;
	}
	
	dlclose(lh);
    } else {
	__unregister_service(i);
	r=0;
    }

    return r;
}

int kh_unload_service(const char *name)
{
    return __unload_service(lookup_kocl_sitem(name));
}

int kh_unload_all_services()
{
    struct list_head *p, *n;
    int e=0;

    list_for_each_safe(p, n, &services) {
	e += __unload_service(list_entry(p, struct _kocl_sitem, list));
    }
    return e;
}
