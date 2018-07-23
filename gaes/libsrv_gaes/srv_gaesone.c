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
#include <CL/cl.h>
#include "../../kocl/kocl.h"
#include "../../kocl/gputils.h"
#include "../gaesu.h"
#include <string.h>

#define BYTES_PER_BLOCK  1024
#define BYTES_PER_THREAD 4
#define BYTES_PER_GROUP  16
#define THREAD_PER_BLOCK (BYTES_PER_BLOCK/BYTES_PER_THREAD)
#define WORDS_PER_BLOCK (BYTES_PER_BLOCK/4)

#define BPT_BYTES_PER_BLOCK 4096
#define MAX_SOURCE_SIZE 1024000

struct kocl_service gaes_ecb_enc_srv;
struct kocl_service gaes_ecb_dec_srv;

struct gaes_ecb_data {
    u32 *d_key;
    u32 *h_key;
    int nrounds;
    int nr_dblks_per_tblk;
};

cl_program program, program2;
cl_kernel decrypt_kernel, encrypt_kernel ;
cl_kernel decrypt_kernel2, encrypt_kernel2 ;

cl_mem  key_dec_buf, key_enc_buf,OutputBuf;
char *cl_filename = "gaes.cl";
char *source_str;
size_t source_size;

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
    decrypt_kernel = clCreateKernel(program, "aes_decrypt_bpt", &ret);
    cl_err(ret);   
    encrypt_kernel = clCreateKernel(program, "aes_encrypt_bpt", &ret);
    cl_err(ret);
  //  printf("gaes service_CLsetup 1 ok \n"); 

    //Build OpenCL kernel for platform2 devices 
    program2 = clCreateProgramWithSource(plat->platform2.context , 1 ,(const char**)&source_str,(const size_t *)&source_size , &ret);
    cl_err(ret);
    ret = clBuildProgram(program2, plat->platform2.numDevices, plat->platform2.devices, NULL,  NULL,  NULL);
    decrypt_kernel2 = clCreateKernel(program2, "aes_decrypt_bpt", &ret);
    cl_err(ret);   
    encrypt_kernel2 = clCreateKernel(program2, "aes_encrypt_bpt", &ret);
    cl_err(ret);
  //  printf("gaes service_CLsetup 2 ok \n"); 

   return 0;
}

int gaes_ecb_compute_size_bpt(struct kocl_service_request *sr)
{   
    sr->global_x = sr->outsize/16;
   /* if(sr->outsize/16>=256){
        sr->local_x =256;
    }else if(sr->outsize/16>=32){
        sr->local_x =32;
    }else{
        sr->local_x =1; 
    }*/
   sr->local_x =32;

    //sr->global_y = 1;  
    //sr->local_y = 1;
 /*
    sr->block_x =
	sr->outsize>=BPT_BYTES_PER_BLOCK?
	BPT_BYTES_PER_BLOCK/16: sr->outsize/16;
    sr->grid_x =
	
    sr->block_y = 1;
    sr->grid_y = 1;
  */
#if DEBUG
    printf("sr->global_x = %d \n",sr->global_x);
    printf("sr->local_x  = %d \n",sr->local_x); 
    printf("compute_size ok \n");  
#endif

    return 0;
}

int gaes_ecb_launch_bpt(struct kocl_service_request *sr)
{
    size_t globalWorkSize[2]={sr->global_x,1};//global work-items
    size_t  workGroupSize[2]={sr->local_x, 1};//work-items per Group 

cl_err(clEnqueueNDRangeKernel( sr->queue, sr->kernel , 2 , NULL , globalWorkSize, NULL/*workGroupSize*/, 0 , NULL , NULL));  

#if DEBUG
    printf("clEnqueueNDRangeKernel ok \n");   
#endif        
    return 0;
}

int gaes_ecb_prepare(struct kocl_service_request *sr)
{
    cl_int ret;
      
    struct crypto_aes_ctx *hctx = (struct crypto_aes_ctx*)sr->hdata;
    u32 key_length=hctx->key_length/4+6; 
   
     if (!strcmp(sr->s->name,"gaes_ecb-dec")){
           sr->key_dec_buf = clCreateBuffer( sr->context, CL_MEM_READ_WRITE , 
                                  sizeof(u32)*AES_MAX_KEYLENGTH_U32 , NULL , &ret); 
        cl_err(ret); 

        if(sr->channel==2 || sr->channel==3 ){
                sr->kernel=decrypt_kernel2;
        }else{
                sr->kernel=decrypt_kernel;
        }     

        cl_err(clSetKernelArg(sr->kernel,0,sizeof(cl_mem), (void*)&sr->key_dec_buf)); 
        cl_err(clEnqueueWriteBuffer( sr->queue , sr->key_dec_buf , CL_FALSE , 0 , 
                                      sizeof(u32)*AES_MAX_KEYLENGTH_U32 , (u32*)&hctx->key_dec , 0 , NULL, NULL));                
     
     }else{
           sr->key_enc_buf = clCreateBuffer( sr->context, CL_MEM_READ_WRITE , 
                                  sizeof(u32)*AES_MAX_KEYLENGTH_U32 , NULL , &ret);            
           cl_err(ret);
            
            if(sr->channel==2 || sr->channel==3 ){
                sr->kernel=encrypt_kernel2;
            }else{
                sr->kernel=encrypt_kernel;
            }                         
           cl_err(clSetKernelArg(sr->kernel,0,sizeof(cl_mem), (void*)&sr->key_enc_buf));
           cl_err(clEnqueueWriteBuffer( sr->queue , sr->key_enc_buf , CL_FALSE , 0 , 
                                      sizeof(u32)*AES_MAX_KEYLENGTH_U32 , (u32*)&hctx->key_enc , 0 , NULL, NULL));                            
     }      

        sr->OutputBuf = clCreateBuffer( sr->context, CL_MEM_READ_WRITE , sr->outsize , NULL , &ret); 
        cl_err(ret);
        cl_err(clSetKernelArg(sr->kernel,1,sizeof(cl_int),  (void*)&key_length)); 
        cl_err(clSetKernelArg(sr->kernel,2,sizeof(cl_mem), (void*)&sr->OutputBuf));  
        //copy input data to OpenCL buff                                
        cl_err(clEnqueueWriteBuffer( sr->queue , sr->OutputBuf , CL_FALSE , 0 , 
                                        sr->outsize , sr->hout , 0 , NULL, NULL));  
    
    return 0;
}

int gaes_ecb_post(struct kocl_service_request *sr)
{  
     cl_err(clEnqueueReadBuffer( sr->queue , sr->OutputBuf , CL_FALSE , 0 ,
                                    sr->outsize , sr->hout , 0 , NULL, NULL));
    
    if (!strcmp(sr->s->name,"gaes_ecb-dec")){
       clReleaseMemObject(sr->key_dec_buf);
    }else{
       clReleaseMemObject(sr->key_enc_buf);
    }
  
    //clReleaseKernel(sr->kernel);    
    clReleaseMemObject(sr->OutputBuf);      
    
    return 0;
}

/*
 * Naming convention of ciphers:
 * g{algorithm}_{mode}[-({enc}|{dev})]
 *
 * {}  : var value
 * []  : optional
 * (|) : or
 */
int init_service(void *lh, int (*reg_srv)(struct kocl_service*, void*))
{
    int err;
    printf("[libsrv_gaes] Info: init gaes onecopy services\n");
   
    
    sprintf(gaes_ecb_enc_srv.name, "gaes_ecb-enc");
    gaes_ecb_enc_srv.sid = 0;
    gaes_ecb_enc_srv.compute_size = gaes_ecb_compute_size_bpt;
    gaes_ecb_enc_srv.launch = gaes_ecb_launch_bpt;
    gaes_ecb_enc_srv.prepare = gaes_ecb_prepare;
    gaes_ecb_enc_srv.post = gaes_ecb_post;
    
    sprintf(gaes_ecb_dec_srv.name, "gaes_ecb-dec");
    gaes_ecb_dec_srv.sid = 0;
    gaes_ecb_dec_srv.compute_size = gaes_ecb_compute_size_bpt;
    gaes_ecb_dec_srv.launch = gaes_ecb_launch_bpt;
    gaes_ecb_dec_srv.prepare = gaes_ecb_prepare;
    gaes_ecb_dec_srv.post = gaes_ecb_post;

    
    err = reg_srv(&gaes_ecb_enc_srv, lh);
    err |= reg_srv(&gaes_ecb_dec_srv, lh);
   
    if (err) {
    	fprintf(stderr,
		"[libsrv_gaes] Error: failed to register gaes services\n");
    } 
    
    return err;
}

int finit_service(void *lh, int (*unreg_srv)(const char*))
{
    int err;
    printf("[libsrv_gaes] Info: finit gaes services\n");
    
    err = unreg_srv(gaes_ecb_enc_srv.name);
    err |= unreg_srv(gaes_ecb_dec_srv.name);
    
    if (err) {
    	fprintf(stderr,
		"[libsrv_gaes] Error: failed to unregister gaes services\n");
    }
    
    return err;
}


