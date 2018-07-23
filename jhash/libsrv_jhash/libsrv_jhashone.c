/* This work is licensed under the terms of the GNU GPL, version 2.  See
 * the GPL-COPYING file in the top-level directory.
 *
 * Copyright (c) 2010-2011 University of Utah and the Flux Group.
 * All rights reserved.
 * 
 * Copyright (c) 2017-2018 NCKU of Taiwan and the ASRLab.
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <CL/cl.h>
#include "../../kocl/kocl.h"
#include "../../kocl/gputils.h"

#define MAX_SOURCE_SIZE 1024000

cl_program program, program2;
cl_kernel kernel, kernel2 ;

char *cl_filename = "jhash_ker.cl";
char *source_str;
size_t source_size;

//static int LoadKernel(char *cl_filename, char **source_str, size_t *source_size);
/*Load the Kernel*/
static int LoadKernel(char *cl_filename, char **source_str, size_t *source_size)
{
    FILE *fp;
    fp = fopen(cl_filename, "r");
    if (!fp) {
        fprintf(stderr, "Failed to load kernel.\n");
        exit(1);
    }

    *source_str = (char*)malloc(MAX_SOURCE_SIZE);
    *source_size = fread(*source_str, 1, MAX_SOURCE_SIZE, fp);
    fclose(fp);
    return 0;
}

int service_CLsetup(struct plat_set *plat){

    cl_int ret;
    LoadKernel( cl_filename, &source_str, &source_size); 
    //Build OpenCL kernel for platform1 devices   
    program = clCreateProgramWithSource(plat->platform1.context , 1 ,(const char**)&source_str,(const size_t *)&source_size , &ret);
    cl_err(ret);
    ret = clBuildProgram(program, plat->platform1.numDevices, plat->platform1.devices, NULL,  NULL,  NULL);
    cl_err(ret);   
    kernel = clCreateKernel(program, "jhash", &ret);
    cl_err(ret);  

    //printf("jhash service_CLsetup 1 ok \n"); 
    //Build OpenCL kernel for platform2 devices 
    program2 = clCreateProgramWithSource(plat->platform2.context , 1 ,(const char**)&source_str,(const size_t *)&source_size , &ret);
    cl_err(ret);
    ret = clBuildProgram(program2, plat->platform2.numDevices, plat->platform2.devices, NULL,  NULL,  NULL);
    cl_err(ret);   
    kernel2 = clCreateKernel(program2, "jhash", &ret);
    cl_err(ret);   

   //printf("jhash service_CLsetup 2 ok \n"); 
   return 0;
}

static int jhash_cs(struct kocl_service_request *sr)
{
    sr->global_x = sr->insize/1024 ; //3072*1024   
    sr->local_x = 32;

   // sr->local_x=(sr->global_x >= 512)? 32 :4 ; 
   
    sr->global_y = 1;
    sr->local_y = 1;

#if DEBUG
    printf("sr->global_x = %d \n",sr->global_x);
    printf("sr->local_x_x = %d \n",sr->local_x); 
    printf("compute_size ok \n");  
#endif
    return 0;
}

static int jhash_launch(struct kocl_service_request *sr)
{
  
    size_t globalWorkSize[2]={sr->global_x,1};//global work-items
    size_t  workGroupSize[2]={sr->local_x, 1};//work-items per Group
    
   // cl_event kernel_event;   
    cl_err(clEnqueueNDRangeKernel( sr->queue, sr->kernel , 2 , NULL , globalWorkSize, NULL, 0 , NULL, NULL/*&kernel_event*/));
  //  clWaitForEvents(1, &kernel_event); 

#if DEBUG
    printf("clEnqueueNDRangeKernel ok \n");   
#endif
    return 0;
}

static int jhash_prepare(struct kocl_service_request *sr)
{
    cl_int ret;       
    sr->InputBuf = clCreateBuffer( sr->context, CL_MEM_READ_WRITE , sr->insize , NULL , &ret); 
    cl_err(ret);
    sr->OutputBuf = clCreateBuffer( sr->context, CL_MEM_READ_WRITE , sr->outsize , NULL , &ret); 
    cl_err(ret);
    
    if(sr->channel==2 || sr->channel==3 ){
            sr->kernel=kernel2;
        }else{
            sr->kernel=kernel;
        }   

    cl_err(clSetKernelArg(sr->kernel,0,sizeof(cl_mem), &sr->InputBuf));   
    cl_err(clSetKernelArg(sr->kernel,1,sizeof(cl_mem), &sr->OutputBuf)); 
   
    cl_err(clEnqueueWriteBuffer( sr->queue , sr->InputBuf , CL_FALSE , 0 , sr->insize , sr->hin , 0 , NULL, NULL));
#if DEBUG
     printf("clEnqueueWriteBuffer ok \n");  
#endif
    return 0;
}

static int jhash_post(struct kocl_service_request *sr)
{   
    
    cl_err(clEnqueueReadBuffer( sr->queue , sr->OutputBuf , CL_FALSE , 0 , sr->outsize , sr->hout , 0 , NULL, NULL));
    
    clReleaseMemObject(sr->InputBuf);
    clReleaseMemObject(sr->OutputBuf);

#if DEBUG
     printf("clEnqueueReadBuffer ok \n");  
#endif
    return 0;
}


static struct kocl_service jhash_srv;

int init_service(void *lh, int (*reg_srv)(struct kocl_service*, void*))
{
    printf("[libsrv_jhash] Info: init jhash_onecopy_service\n");
   
    sprintf(jhash_srv.name, "jhash_service");
    jhash_srv.sid = 1;
    jhash_srv.compute_size = jhash_cs;
    jhash_srv.launch = jhash_launch;
    jhash_srv.prepare = jhash_prepare;
    jhash_srv.post = jhash_post;

    return reg_srv(&jhash_srv, lh);
}

int finit_service(void *lh, int (*unreg_srv)(const char*))
{
    printf("[libsrv_jhash] Info: finit test service\n");
    return unreg_srv(jhash_srv.name);
}
