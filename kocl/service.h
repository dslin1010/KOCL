/*
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the GPL-COPYING file in the top-level directory.
 *
 * Copyright (c) 2010-2011 University of Utah and the Flux Group.
 * All rights reserved.
 *
 */

#ifndef __SERVICE_H__
#define __SERVICE_H__

struct kocl_service {
    char name[KOCL_SERVICE_NAME_SIZE];
    int sid;
    int (*compute_size)(struct kocl_service_request *sreq);
    int (*launch)(struct kocl_service_request *sreq);
    int (*prepare)(struct kocl_service_request *sreq);
    int (*post)(struct kocl_service_request *sreq);
};

struct plat_arg{
        cl_uint numDevices;
     cl_device_id *devices;
       cl_context context ;
}; 

struct plat_set{
    struct plat_arg platform1;
    struct plat_arg platform2;
};

#define SERVICE_INIT "init_service"
#define SERVICE_FINIT "finit_service"
#define SERVICE_LIB_PREFIX "libsrv_"
#define SERVICE_CL "service_CLsetup"
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS //Should define before #include <CL/cl.h>
#include <CL/cl.h>

typedef int (*fn_init_service)(
    void* libhandle, int (*reg_srv)(struct kocl_service *, void*));
typedef int (*fn_finit_service)(
    void* libhandle, int (*unreg_srv)(const char*));

typedef int (*CLsetup)(struct plat_set *plat);

#ifdef __KOCL__

struct kocl_service * kh_lookup_service(const char *name);
int kh_register_service(struct kocl_service *s, void *libhandle);
int kh_unregister_service(const char *name);
int kh_load_service(const char *libpath);
int kh_load_all_services(const char *libdir);
int kh_unload_service(const char *name);
int kh_unload_all_services();

#endif /* __KOCL__ */

#endif
