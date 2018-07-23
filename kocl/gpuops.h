/*
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the GPL-COPYING file in the top-level directory.
 *
 *  Copyright (c) 2017-2018 NCKU of Taiwan and the ASRLab.
 * 
 *  Management of the OpenCL accelerator(s).
 */


#ifndef __GPUOPS_H__
#define __GPUOPS_H__

#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#include <CL/cl.h>

 void gpu_init();
 void gpu_finit();

 void service_CLset(int (*CLsetup)(struct plat_set *plat));

 void **gpu_alloc_pinned_mem(unsigned long size);
 void gpu_free_pinned_mem(struct kocl_gpu_mem_info *p);
 
 int gpu_alloc_device_mem(struct kocl_service_request *sreq);
 void gpu_free_device_mem(struct kocl_service_request *sreq);
 int gpu_alloc_cmdQueue(struct kocl_service_request *sreq);
 void gpu_free_cmdQueue(struct kocl_service_request *sreq);

 int gpu_execution_finished(struct kocl_service_request *sreq);
 int gpu_post_finished(struct kocl_service_request *sreq);

 cl_command_queue gpu_get_cmdQueue(struct kocl_service_request *sreq);

 int GetHw();

#endif