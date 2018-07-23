/*
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the GPL-COPYING file in the top-level directory.
 *
 * Copyright (c) 2010-2011 University of Utah and the Flux Group.
 * All rights reserved.
 *
 * Copyright (c) 2017-2018 NCKU of Taiwan and the ASRLab.
 * Common header for userspace helper module , kernel KOCL service module and OS service module.
 *
 */

#ifndef __KOCL_H__
#define __KOCL_H__

#define TO_UL(v) ((unsigned long)(v))

#define ADDR_WITHIN(pointer, base, size)		\
    (TO_UL(pointer) >= TO_UL(base) &&			\
     (TO_UL(pointer) < TO_UL(base)+TO_UL(size)))

#define ADDR_REBASE(dst_base, src_base, pointer)			\
    (TO_UL(dst_base) + (						\
	TO_UL(pointer)-TO_UL(src_base)))

struct kocl_gpu_mem_info {
    void * uva;
    void * uva2;
    void * uva3;
    unsigned long size;
};

#define KOCL_SERVICE_NAME_SIZE 32

struct kocl_ku_request {
    int id;
    int channel;
    char service_name[KOCL_SERVICE_NAME_SIZE];
    void *in, *out, *data;
    unsigned long insize, outsize, datasize;
};

/* kocl's errno */
#define KOCL_OK 0
#define KOCL_NO_RESPONSE 1
#define KOCL_NO_SERVICE 2
#define KOCL_TERMINATED 3

struct kocl_ku_response {
    int id;
    int errcode;
};

/*
 * Only for kernel code or helper
 */
#if defined __KERNEL__ || defined __KOCL__

/* the NR will not be used */
#define KOCL_BUF_NR 1
#define KOCL_BUF_SIZE (1024*1024*128)

#define KOCL_MMAP_SIZE KOCL_BUF_SIZE

#define KOCL_DEV_NAME "kocl"

/* ioctl */
#include <linux/ioctl.h>

#define KOCL_IOC_MAGIC 'g'

#define KOCL_IOC_SET_GPU_BUFS \
    _IOW(KOCL_IOC_MAGIC, 1, struct kocl_gpu_mem_info[KOCL_BUF_NR])
#define KOCL_IOC_GET_GPU_BUFS \
    _IOR(KOCL_IOC_MAGIC, 2, struct kocl_gpu_mem_info[KOCL_BUF_NR])
#define KOCL_IOC_SET_STOP     _IO(KOCL_IOC_MAGIC, 3)
#define KOCL_IOC_GET_REQS     _IOR(KOCL_IOC_MAGIC, 4, 

#define KOCL_IOC_MAXNR 4

#include "kocl_log.h"

#endif /* __KERNEL__ || __KOCL__  */

/*
 * For helper and service providers
 */
#ifndef __KERNEL__

#define CL_USE_DEPRECATED_OPENCL_1_2_APIS //Should define before #include <CL/cl.h>
#include <CL/cl.h>
struct kocl_service;

struct kocl_service_request {
    int id;
    int channel;
    void *hin, *hout, *hdata;
    void *din, *dout, *ddata;
    unsigned long insize, outsize, datasize;
    int errcode;
    struct kocl_service *s;
    int global_x, global_y;
    int local_x, local_y;
    int state;
    int queue_id;
    cl_command_queue queue;   
    cl_context context;
    cl_uint numDevices;
    cl_device_id *devices;
    cl_mem  InputBuf,OutputBuf ;
    cl_mem  key_dec_buf, key_enc_buf; 
    cl_kernel kernel ;  
};

/* service request states: */
#define KOCL_REQ_INIT 1
#define KOCL_REQ_MEM_DONE 2
#define KOCL_REQ_PREPARED 3
#define KOCL_REQ_RUNNING 4
#define KOCL_REQ_POST_EXEC 5
#define KOCL_REQ_DONE 6

#include "service.h"

#endif /* no __KERNEL__ */

/*
 * For kernel code only
 */
#ifdef __KERNEL__

#include <linux/list.h>
#include <linux/mm.h>


struct kocl_request;

typedef int (*kocl_callback)(struct kocl_request *req);

struct kocl_request {
    int id;
    int channel;
    void *in, *out, *udata, *kdata;
    unsigned long insize, outsize, udatasize, kdatasize;
    char service_name[KOCL_SERVICE_NAME_SIZE];
    kocl_callback callback;
    int errcode;
    struct completion *c;/* async-call completion */
};

extern int kocl_offload_sync(struct kocl_request*);
extern int kocl_offload_async(struct kocl_request*);

extern int kocl_next_request_id(void);
extern struct kocl_request* kocl_alloc_request(void);
extern void kocl_free_request(struct kocl_request*);

extern void *kocl_malloc(unsigned long nbytes,int channel);
extern void kocl_free(void* p,int channel);


#endif /* __KERNEL__ */

#endif
