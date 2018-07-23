/*
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the GPL-COPYING file in the top-level directory.
 *
 * Copyright (c) 2010-2011 University of Utah and the Flux Group.
 * All rights reserved.
 *
 * Copyright (c) 2017-2018 NCKU of Taiwan and the ASRLab.
 * 
 *  Management of the OpenCL accelerator(s).
 */


#include <stdlib.h>
#include <stdio.h>
#include "helper.h"
#include "gputils.h"
#include "gpuops.h"
#include "service.h"

#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#include <CL/cl.h>

#define MAX_QUEUE_NR 8
cl_int ret;
cl_uint numPlatforms = 0;
cl_platform_id *platforms = NULL;
cl_uint numDevices = 0, numDevices2 = 0;
cl_device_id *devices = NULL,*devices2 = NULL;
cl_context context = NULL, context2 = NULL;

cl_command_queue  cmdQueue[MAX_QUEUE_NR], cmdQueue2[MAX_QUEUE_NR], cmdQueue3[MAX_QUEUE_NR];
cl_command_queue  mapQueue, mapQueue2, mapQueue3;
static int Queueuses[MAX_QUEUE_NR], Queueuses2[MAX_QUEUE_NR], Queueuses3[MAX_QUEUE_NR];

char   plat_name[10240];
char device_name[10240];

cl_mem  hostPinBuf, hostPinBuf2, hostPinBuf3;

void * buff_arr[3]={0};  



/*Get the OpenCL platforms and devices */
int GetHw(){    

    
    cl_err(clGetPlatformIDs(0, NULL, &numPlatforms));
    platforms =(cl_platform_id*)malloc(numPlatforms*sizeof(cl_platform_id));

    cl_err(clGetPlatformIDs(numPlatforms, platforms , NULL));   
    
    for(int i=0;i<numPlatforms;++i){        
      cl_err(clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, 10240, plat_name, NULL));
       printf("Platform %d = %s\n", i ,plat_name );
    }

    cl_err(clGetDeviceIDs(platforms[0],CL_DEVICE_TYPE_ALL,0,NULL, &numDevices));
    devices =(cl_device_id*)malloc(numDevices*sizeof(cl_device_id));
    cl_err(clGetDeviceIDs(platforms[0],CL_DEVICE_TYPE_ALL, numDevices , devices ,NULL));	
    for(int i=0;i<numDevices;++i){        
	    cl_err(clGetDeviceInfo(devices[i], CL_DEVICE_NAME, sizeof(device_name), device_name, NULL)); 
        printf("Device %d = %s\n", i, device_name );	 
    }

    cl_err(clGetDeviceIDs(platforms[1],CL_DEVICE_TYPE_ALL,0,NULL, &numDevices2));
    devices2 =(cl_device_id*)malloc(numDevices2*sizeof(cl_device_id));
    cl_err(clGetDeviceIDs(platforms[1],CL_DEVICE_TYPE_ALL, numDevices2 , devices2 ,NULL));   	
    for(int i=0;i<numDevices2;++i){        
	    cl_err(clGetDeviceInfo(devices2[i], CL_DEVICE_NAME, sizeof(device_name), device_name, NULL)); 
         printf("Device %d = %s\n", i, device_name ); 
    }
    
  
   return 0;
}



void gpu_init()
{      
    int i;

    GetHw();
    context = clCreateContext(NULL, numDevices ,devices , NULL, NULL, &ret);//Nvidia Platform1
    cl_err(ret);
    context2 = clCreateContext(NULL, numDevices2 ,devices2 , NULL, NULL, &ret);//Intel Platform2
    cl_err(ret);

    /*Using mapQueue to map the cl_mem buffer to host */
    mapQueue = clCreateCommandQueue( context, devices[0], CL_QUEUE_PROFILING_ENABLE , &ret);//Nvidia GPU
    cl_err(ret);
    mapQueue2 = clCreateCommandQueue( context2, devices2[0], CL_QUEUE_PROFILING_ENABLE , &ret);//HD 530
    cl_err(ret);
    mapQueue3 = clCreateCommandQueue( context2, devices2[1], CL_QUEUE_PROFILING_ENABLE , &ret);//i7 CPU
    cl_err(ret);       
    
    for (i=0; i<MAX_QUEUE_NR; i++) {                            /*CL_QUEUE_PROFILING_ENABLE,CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE*/
        cmdQueue[i]= clCreateCommandQueue( context, devices[0], CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &ret);//Nvidia GPU-> channel 1
        cl_err(ret);
	    Queueuses[i] = 0;
    }  
     for (i=0; i<MAX_QUEUE_NR; i++) {                            /*CL_QUEUE_PROFILING_ENABLE,CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE*/
        cmdQueue2[i]= clCreateCommandQueue( context2, devices2[0], CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &ret);//HD 530-> channel 2
        cl_err(ret);
	    Queueuses2[i] = 0;
    }  
    for (i=0; i<MAX_QUEUE_NR; i++) {                            /*CL_QUEUE_PROFILING_ENABLE,CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE*/
        cmdQueue3[i]= clCreateCommandQueue( context2, devices2[1], CL_QUEUE_OUT_OF_ORDER_EXEC_MODE_ENABLE, &ret);//i7 CPU-> channel 3
        cl_err(ret);
	    Queueuses3[i] = 0;
    }  
    printf("clCreateCommandQueue ok ~\n");     
}

void service_CLset(int (*CLsetup)(struct plat_set *plat)){
    
    struct plat_set plat;

    plat.platform1.numDevices = numDevices;
    plat.platform1.devices = devices;
    plat.platform1.context = context;

    plat.platform2.numDevices = numDevices2;
    plat.platform2.devices = devices2;
    plat.platform2.context = context2;
   
    CLsetup(&plat);
    printf("service_CLset ok ~\n");  
}


void gpu_finit()
{
    int i;
   
    for (i=0; i<MAX_QUEUE_NR; i++) {
	cl_err( clReleaseCommandQueue(cmdQueue[i]));
    }
    for (i=0; i<MAX_QUEUE_NR; i++) {
	cl_err( clReleaseCommandQueue(cmdQueue2[i]));
    }
    for (i=0; i<MAX_QUEUE_NR; i++) {
	cl_err( clReleaseCommandQueue(cmdQueue3[i]));
    }

    clReleaseContext(context);
    clReleaseContext(context2);
    free(devices);
    free(devices2);
    free(platforms);    
}

cl_command_queue gpu_get_cmdQueue(struct kocl_service_request *sreq)
{
    if (sreq->queue_id < 0 || sreq->queue_id >= MAX_QUEUE_NR){
	    return 0;
    }else{
	    if(sreq->channel==2){
            return (cl_command_queue)cmdQueue2[sreq->queue_id];
        }else if(sreq->channel==3){
            return (cl_command_queue)cmdQueue3[sreq->queue_id];
        }else{
            return (cl_command_queue)cmdQueue[sreq->queue_id];
        }
    }
}

/*Allocate Hoast Pinned memory */
void **gpu_alloc_pinned_mem(unsigned long size) {

    void *h, *h2, *h3;    
    cl_event map_event;

    hostPinBuf = clCreateBuffer( context, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR , size , NULL , &ret);
    cl_err(ret);
    h = clEnqueueMapBuffer( mapQueue, hostPinBuf, CL_TRUE , CL_MAP_WRITE, 0 , size , 0 , NULL, &map_event, &ret);
    cl_err(ret);
    clWaitForEvents(1, &map_event);

    hostPinBuf2 = clCreateBuffer( context2, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR , size , NULL , &ret);
    cl_err(ret);
    h2 =clEnqueueMapBuffer( mapQueue2, hostPinBuf2, CL_TRUE , CL_MAP_WRITE, 0 , size , 0 , NULL, &map_event, &ret);
    cl_err(ret);
    clWaitForEvents(1, &map_event);

    hostPinBuf3 = clCreateBuffer( context2, CL_MEM_READ_WRITE | CL_MEM_ALLOC_HOST_PTR , size , NULL , &ret);
    cl_err(ret);
    h3 =clEnqueueMapBuffer( mapQueue3, hostPinBuf3, CL_TRUE , CL_MAP_WRITE, 0 , size , 0 , NULL, &map_event, &ret);
    cl_err(ret);
    clWaitForEvents(1, &map_event);
   
    buff_arr[0]=h;  //Nvidia GPU
    buff_arr[1]=h2; //HD 530
    buff_arr[2]=h3; //i7 cpu

    return buff_arr;
}


void gpu_free_pinned_mem(struct kocl_gpu_mem_info *p) {

    cl_event map_event; 
    clEnqueueUnmapMemObject(mapQueue, hostPinBuf , p->uva , 0 , NULL , &map_event); 
    clWaitForEvents(1, &map_event);
    clReleaseMemObject(hostPinBuf);
    
    clEnqueueUnmapMemObject(mapQueue2, hostPinBuf2 , p->uva2 , 0 , NULL , &map_event); 
    clWaitForEvents(1, &map_event);
    clReleaseMemObject(hostPinBuf2);

    clEnqueueUnmapMemObject(mapQueue3, hostPinBuf3 , p->uva3 , 0 , NULL , &map_event); 
    clWaitForEvents(1, &map_event);
    clReleaseMemObject(hostPinBuf3);
}

static int __check_cmdQueue_done(cl_command_queue Q)
{
    cl_int e = clFinish(Q);
    if (e == CL_SUCCESS) {
	    return 1;
      } 
    else 
	  cl_err(e);
    return 0;
}

int gpu_execution_finished(struct kocl_service_request *sreq)
{
    cl_command_queue Q = (cl_command_queue)gpu_get_cmdQueue(sreq);
    return __check_cmdQueue_done(Q);
}

int gpu_post_finished(struct kocl_service_request *sreq)
{
    cl_command_queue Q = (cl_command_queue)gpu_get_cmdQueue(sreq);
    return __check_cmdQueue_done(Q);
}

/*set platform context args */
int gpu_alloc_device_mem(struct kocl_service_request *sreq)
{           
     if(sreq->channel==2 || sreq->channel==3 ){
             sreq->context=context2;
        }else{
            sreq->context=context;
        }   
    return 0;
}


int gpu_alloc_cmdQueue(struct kocl_service_request *sreq)
{
    int i;

     if(sreq->channel==2){
            for (i=0; i<MAX_QUEUE_NR; i++) {
	            if (!Queueuses2[i]) {
	                Queueuses2[i] = 1;
	                sreq->queue_id = i;
	                sreq->queue = (cl_command_queue)(cmdQueue2[i]);             
	                return 0;
	            }         
            }
    }else if(sreq->channel==3){
             for (i=0; i<MAX_QUEUE_NR; i++) {
	            if (!Queueuses3[i]) {
	                Queueuses3[i] = 1;
	                sreq->queue_id = i;
	                sreq->queue = (cl_command_queue)(cmdQueue3[i]);             
	                return 0;
	            }         
            }
    }else{//channel 0 or 1
            for (i=0; i<MAX_QUEUE_NR; i++) {
	            if (!Queueuses[i]) {
	                Queueuses[i] = 1;
	                sreq->queue_id = i;
	                sreq->queue = (cl_command_queue)(cmdQueue[i]);             
	                return 0;
	            }         
            }
    }    
    return 1;
}

void gpu_free_cmdQueue(struct kocl_service_request *sreq)
{
    if (sreq->queue_id >= 0 && sreq->queue_id < MAX_QUEUE_NR) {
        if(sreq->channel==2){
	        Queueuses2[sreq->queue_id] = 0;
        }else if(sreq->channel==3){
            Queueuses3[sreq->queue_id] = 0;
        }else{
            Queueuses[sreq->queue_id] = 0;
        }    
    }
}


