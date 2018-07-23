/*
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the GPL-COPYING file in the top-level directory.
 *
 * Copyright (c) 2010-2011 University of Utah and the Flux Group.
 * All rights reserved.
 *
 * Copyright (c) 2017-2018 NCKU of Taiwan and the ASRLab.
 * 
 * Userspace helper module.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include "list.h"
#include "helper.h"
#include "gpuops.h"

struct _kocl_sritem {
    struct kocl_service_request sr;
    struct list_head glist;
    struct list_head list;
};

static int devfd;

struct kocl_gpu_mem_info hostbuf;

volatile int kh_loop_continue = 1;

static char *service_lib_dir;
static char *kocldev;

/* lists of requests of different states */
LIST_HEAD(all_reqs);
LIST_HEAD(init_reqs);
LIST_HEAD(memdone_reqs);
LIST_HEAD(prepared_reqs);
LIST_HEAD(running_reqs);
LIST_HEAD(post_exec_reqs);
LIST_HEAD(done_reqs);

#define ssc(...) _safe_syscall(__VA_ARGS__, __FILE__, __LINE__)

int _safe_syscall(int r, const char *file, int line)
{
    if (r<0) {
	fprintf(stderr, "Error in %s:%d, ", file, line);
	perror("");
	abort();
    }
    return r;
}


static int kh_init(void)
{
    int  i, len, r;
    void **p;
    
    devfd = ssc(open(kocldev, O_RDWR));

    /*GetHW and Load the OpenCL kernel code */
    gpu_init();    
    kh_log(KOCL_LOG_PRINT,"gpu_init() ok\n");

    /* alloc GPU Pinned memory buffers */
    p = (void**)gpu_alloc_pinned_mem(KOCL_BUF_SIZE+PAGE_SIZE); 

    hostbuf.uva = p[0];
    hostbuf.uva2 = p[1];
    hostbuf.uva3 = p[2];
    hostbuf.size = KOCL_BUF_SIZE;

    kh_log(KOCL_LOG_PRINT,"hostbuf.uva1 :%p \n", hostbuf.uva);
    kh_log(KOCL_LOG_PRINT,"hostbuf.uva2 :%p \n", hostbuf.uva2);
    kh_log(KOCL_LOG_PRINT,"hostbuf.uva3 :%p \n", hostbuf.uva3);

    memset(hostbuf.uva, 0, KOCL_BUF_SIZE);//防止copy on write 所以每個page配給它一個值
    ssc( mlock(hostbuf.uva, KOCL_BUF_SIZE));
    memset(hostbuf.uva2, 0, KOCL_BUF_SIZE);//防止copy on write 所以每個page配給它一個值
    ssc( mlock(hostbuf.uva2, KOCL_BUF_SIZE));
     memset(hostbuf.uva3, 0, KOCL_BUF_SIZE);//防止copy on write 所以每個page配給它一個值
    ssc( mlock(hostbuf.uva3, KOCL_BUF_SIZE));

    /* tell kernel the buffers */
    r = ioctl(devfd, KOCL_IOC_SET_GPU_BUFS, (unsigned long)&hostbuf);
    if (r < 0) {
	perror("Write req file for buffers.");
	abort();
    }

    return 0;
}


static int kh_finit(void)
{
    int i;

    ioctl(devfd, KOCL_IOC_SET_STOP);
    close(devfd);
    gpu_finit();

    gpu_free_pinned_mem(&hostbuf);

    return 0;
}

static int kh_send_response(struct kocl_ku_response *resp)
{
    ssc(write(devfd, resp, sizeof(struct kocl_ku_response)));
    return 0;
}

static void kh_fail_request(struct _kocl_sritem *sreq, int serr)
{
    sreq->sr.state = KOCL_REQ_DONE;
    sreq->sr.errcode = serr;
    list_del(&sreq->list);
    list_add_tail(&sreq->list, &done_reqs);
}

static struct _kocl_sritem *kh_alloc_service_request()
{
    struct _kocl_sritem *s = (struct _kocl_sritem *)
	malloc(sizeof(struct _kocl_sritem));
    if (s) {
    	memset(s, 0, sizeof(struct _kocl_sritem));
	INIT_LIST_HEAD(&s->list);
	INIT_LIST_HEAD(&s->glist);
    }
    return s;
}

static void kh_free_service_request(struct _kocl_sritem *s)
{
    free(s);
}

static void kh_init_service_request(struct _kocl_sritem *item,
			       struct kocl_ku_request *kureq)
{
    list_add_tail(&item->glist, &all_reqs);

    memset(&item->sr, 0, sizeof(struct kocl_service_request));
    item->sr.id = kureq->id;
    item->sr.hin = kureq->in;
    item->sr.hout = kureq->out;
    item->sr.hdata = kureq->data;
    item->sr.insize = kureq->insize;
    item->sr.outsize = kureq->outsize;
    item->sr.datasize = kureq->datasize;
    item->sr.queue_id = -1;
    item->sr.channel= kureq->channel;
    item->sr.s = kh_lookup_service(kureq->service_name); 
    if (!item->sr.s) {
	    dbg("can't find service\n");
	    kh_fail_request(item, KOCL_NO_SERVICE);
    } else {
	    item->sr.s->compute_size(&item->sr);
	    item->sr.state = KOCL_REQ_INIT;
	    item->sr.errcode = 0;
	    list_add_tail(&item->list, &init_reqs);
    }
}

static int kh_get_next_service_request(void)
{
    int err;
    struct pollfd pfd;

    struct _kocl_sritem *sreq;
    struct kocl_ku_request kureq;

    pfd.fd = devfd;
    pfd.events = POLLIN;
    pfd.revents = 0;

    err = poll(&pfd, 1, list_empty(&all_reqs)? -1:0);
    if (err == 0 || (err && !(pfd.revents & POLLIN)) ) {
	return -1;
    } else if (err == 1 && pfd.revents & POLLIN)
    {
	sreq = kh_alloc_service_request();
    
	if (!sreq)
	    return -1;

	err = read(devfd, (char*)(&kureq), sizeof(struct kocl_ku_request));
	if (err <= 0) {
	    if (errno == EAGAIN || err == 0) {
		kh_free_service_request(sreq);
		return -1;
	    } else {
		perror("Read request.");
		abort();
	    }
	} else {
	    kh_init_service_request(sreq, &kureq);	
	    return 0;
	}
    } else {
	if (err < 0) {
	    perror("Poll request");
	    abort();
	} else {
	    fprintf(stderr, "Poll returns multiple fd's results\n");
	    abort();
	}
    }    
}

static int kh_request_alloc_mem(struct _kocl_sritem *sreq)
{
    int r = gpu_alloc_device_mem(&sreq->sr);
    if (r) {
	return -1;
    } else {
	sreq->sr.state = KOCL_REQ_MEM_DONE;
	list_del(&sreq->list);
	list_add_tail(&sreq->list, &memdone_reqs);
	return 0;
    }
}

static int kh_prepare_exec(struct _kocl_sritem *sreq)
{
    int r;
    if (gpu_alloc_cmdQueue(&sreq->sr)) {
	r = -1;
    } else {
	  r = sreq->sr.s->prepare(&sreq->sr);  
	
	if (r) {
	    dbg("%d fails prepare\n", sreq->sr.id);
	    kh_fail_request(sreq, r);
	} else {
	    sreq->sr.state = KOCL_REQ_PREPARED;
	    list_del(&sreq->list);
	    list_add_tail(&sreq->list, &prepared_reqs);
	  }
    }

    return r;
}
	
static int kh_launch_exec(struct _kocl_sritem *sreq)
{
   int r = sreq->sr.s->launch(&sreq->sr); 
    if (r) {
	dbg("%d fails launch\n", sreq->sr.id);
	kh_fail_request(sreq, r);	
    } else {
	sreq->sr.state = KOCL_REQ_RUNNING;
	list_del(&sreq->list);
	list_add_tail(&sreq->list, &running_reqs);
    }
    return 0;
}

static int kh_post_exec(struct _kocl_sritem *sreq)
{
    int r = 1;
    if (gpu_execution_finished(&sreq->sr)){
	  if (!(r=sreq->sr.s->post(&sreq->sr))){  
	      sreq->sr.state = KOCL_REQ_POST_EXEC;
	      list_del(&sreq->list);
	      list_add_tail(&sreq->list, &post_exec_reqs);
	   }
	  else {
	    dbg("%d fails post\n", sreq->sr.id);
	    kh_fail_request(sreq, r);
	    }
    }
    return r;
}

static int kh_finish_post(struct _kocl_sritem *sreq)
{
    if (gpu_post_finished(&sreq->sr)) {
	  sreq->sr.state = KOCL_REQ_DONE;
	  list_del(&sreq->list);
	  list_add_tail(&sreq->list, &done_reqs);
	
	  return 0;
    }

    return 1;
}

static int kh_service_done(struct _kocl_sritem *sreq)
{
    struct kocl_ku_response resp;

    resp.id = sreq->sr.id;
    resp.errcode = sreq->sr.errcode;
    
    kh_send_response(&resp);
    
    list_del(&sreq->list);
    list_del(&sreq->glist);
    gpu_free_cmdQueue(&sreq->sr);   
    kh_free_service_request(sreq);
    return 0;
}

static int __kh_process_request(int (*op)(struct _kocl_sritem *),
			      struct list_head *lst, int once)//assign funciton name 及 list 
{
    struct list_head *pos, *n;
    int r = 0;
    
    list_for_each_safe(pos, n, lst) {//會刪除list上的每個reqs 
	r = op(list_entry(pos, struct _kocl_sritem, list));//取出struct _kocl_sritem 的位址執行op也就是執行function
	if (!r && once)
	    break;
    }

    return r;	
}

static int kh_main_loop()
{    
    while (kh_loop_continue)
    {
	__kh_process_request(kh_service_done, &done_reqs, 0);
	__kh_process_request(kh_finish_post, &post_exec_reqs, 0);
	__kh_process_request(kh_post_exec, &running_reqs, 1);
	__kh_process_request(kh_launch_exec, &prepared_reqs, 1);
	__kh_process_request(kh_prepare_exec, &memdone_reqs, 1);
	__kh_process_request(kh_request_alloc_mem, &init_reqs, 0);
	kh_get_next_service_request();	
    }

    return 0;
}

int main(int argc, char *argv[])
{
    int c;
    kocldev = "/dev/kocl";
    service_lib_dir = "./";

    while ((c = getopt(argc, argv, "d:l:v:")) != -1)
    {
	switch (c)
    {
	case 'd':
	    kocldev = optarg;
	    break;
	case 'l':
	    service_lib_dir = optarg;
	    break;
	case 'v':
	    kocl_log_level = atoi(optarg);
	    break;
	default:
	    fprintf(stderr,
		    "Usage %s"
		    " [-d device]"
		    " [-l service_lib_dir]"
		    " [-v log_level"
		    "\n",
		    argv[0]);
	    return 0;
	  }
    }
    
    kh_init();
    kh_load_all_services(service_lib_dir);
    kh_main_loop();
    kh_finit();
    return 0;
}
