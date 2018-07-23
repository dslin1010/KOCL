/* This work is licensed under the terms of the GNU GPL, version 2.  See
 * the GPL-COPYING file in the top-level directory.
 *
 * Copyright (c) 2010-2011 University of Utah and the Flux Group.
 * All rights reserved.
 * Copyright (c) 2017-2018 NCKU of Taiwan and the ASRLab.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/gfp.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <asm/page.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/moduleparam.h>

#include "../../kocl/kocl.h"

/* customized log function */
#define g_log(level, ...) kocl_do_log(level, "callgpu", ##__VA_ARGS__)
#define dbg(...) g_log(KOCL_LOG_DEBUG, ##__VA_ARGS__)

#define MAX_BLK_SIZE (32*1024)
#define MIN_BLK_SIZE (4)

#define TEST_TIMES 10

static int loop= 10 ;
module_param(loop, int , 0);

static int channel=0;
module_param(channel, int , 0);

int mycb(struct kocl_request *req)
{    
     complete(req->c); 
  //   g_log(KOCL_LOG_PRINT, "REQ Comp: %lu \n",req->c);   
          
    kocl_free(req->out,req->channel);
    kocl_free(req->in,req->channel);
    kocl_free_request(req);         
    return 0;
}

int mycb2(struct kocl_request *req)
{
     g_log(KOCL_LOG_PRINT, "REQ ID: %d, RESP CODE: %d\n",
       req->id, req->errcode);           
    return 0;
}

static int jhash_test(int kb, struct completion *c){

    struct kocl_request* req;   
    int i,k,j;
    char *in;
    unsigned int *out;   
   
            if(channel==0){
                channel=1;
                goto run;
            } 
            if(channel==1){
                channel=0;
                goto run;
            } 
            
run:            
            req = kocl_alloc_request();
                 if (!req) {
	                g_log(KOCL_LOG_ERROR, "request null\n");
	                return 0;
                 }     
            req->channel=channel;

            in = kocl_malloc(1024*kb, req->channel);
  
            for(k=0;k<1024*kb;k++){
	           in[k] = 'k' ;
            }
       
            out = kocl_malloc(sizeof(unsigned int)* kb, req->channel);
            
          /*  for(k=0;k<10;k++)
            {
                 out[k]=0;
                 printk("%d out= %d \n",k,out[k]);
            }*/
            
            req->in = in;
            if (!req->in) {
	            g_log(KOCL_LOG_ERROR, "callgpu out of memory\n");
	            kocl_free_request(req);
	            return 0;
            }
            
            if(c){
                   req->c = c;
                   req->insize = 1024*kb;
                   req->out = out;
                   req->outsize = (sizeof(unsigned int)*kb);
                   strcpy(req->service_name, "jhash_service");
                   req->callback = mycb;
                   kocl_offload_async(req);
                    
                  /*for(k=0;k<10;k++){
                      printk("%d out= %d \n",k,out[k]);
                   }*/
            }else{              
                   req->insize = 1024*kb;
                   req->out = out;
                   req->outsize = (sizeof(unsigned int)*kb);
                   strcpy(req->service_name, "jhash_service");
                   req->callback = mycb2;
                   kocl_offload_sync(req);
                   /*for(k=0;k<10;k++){
                      printk("%d out= %d \n",k,out[k]);
                    }*/
                         
                   kocl_free(req->out, req->channel);
                   kocl_free(req->in, req->channel);
                   kocl_free_request(req);   
            }

   return 0;
}


static int __init minit(void)
{
    g_log(KOCL_LOG_PRINT, "loaded\n");
    struct timeval t0, t1;
    unsigned int i,kb;
    long tt;
    struct completion *cs=NULL;
    
    cs = (struct completion*)kmalloc(sizeof(struct completion)*loop,GFP_KERNEL);
    tt=0;
   // kb=32768;
    
for (kb = MIN_BLK_SIZE; kb <= MAX_BLK_SIZE; kb <<= 1) {

    if(cs){
            do_gettimeofday(&t0); 
            for(i=0;i<loop;++i){
               init_completion(cs+i); 
               jhash_test( kb, cs+i);
            }
            for(i=0;i<loop;++i){
              wait_for_completion_interruptible(cs+i);
            }
            do_gettimeofday(&t1); 
            tt = 1000000*(t1.tv_sec-t0.tv_sec)+((long)(t1.tv_usec) - (long)(t0.tv_usec));       
             printk("Size: %10u ,async Time:  %10lu us \n",kb ,tt/loop);
    }else{
            tt=0;
            do_gettimeofday(&t0); 
            for(i=0;i<loop;++i){
               jhash_test( kb, NULL);
            }
            do_gettimeofday(&t1);
       
            tt = 1000000*(t1.tv_sec-t0.tv_sec)+((long)(t1.tv_usec) - (long)(t0.tv_usec));       
            printk(" Size: %10u ,sync Time:  %10lu us \n",kb , tt/loop ); 
    }                  
}            
   kfree(cs);

   return 0;
}

static void __exit mexit(void)
{
    g_log(KOCL_LOG_PRINT, "unload\n");
}

module_init(minit);
module_exit(mexit);

MODULE_LICENSE("GPL");
