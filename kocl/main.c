/*
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the GPL-COPYING file in the top-level directory.
 *
 * Copyright (c) 2010-2011 University of Utah and the Flux Group.
 * All rights reserved.
 * 
 * KGPU k-u communication module.
 * Weibin Sun
 * 
 * Copyright (c) 2017-2018 NCKU of Taiwan and the ASRLab.
 * We extended KGPU to KOCL service module.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/gfp.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/bitmap.h>
#include <linux/device.h>
#include <asm/atomic.h>
#include <asm/page.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include "kkocl.h"
#include "dedup.h"

struct _kocl_mempool {
    unsigned long uva;    
    unsigned long kva;    
    struct page **pages;    
    u32 npages;
    u32 nunits;
    unsigned long *bitmap;
    u32           *alloc_sz;
};

struct _kocl_dev {
    struct cdev cdev;
    struct class *cls;
    dev_t devno;

    int rid_sequence;
    spinlock_t ridlock;

    struct list_head reqs;
    spinlock_t reqlock;
    wait_queue_head_t reqq;

    struct list_head rtdreqs;
    spinlock_t rtdreqlock;

    struct _kocl_mempool gmpool;
    struct _kocl_mempool gmpool2;
    struct _kocl_mempool gmpool3;    
    spinlock_t gmpool_lock;    
    int state;
};

struct _kocl_request_item {
    struct list_head list;
    struct kocl_request *r;
};

struct _kocl_sync_call_data {
	wait_queue_head_t queue;
	void* oldkdata;
	kocl_callback oldcallback;
	int done;
};

static atomic_t kocldev_av = ATOMIC_INIT(1);
static struct _kocl_dev kocldev;

static struct kmem_cache *kocl_request_cache;
static struct kmem_cache *kocl_request_item_cache;
static struct kmem_cache *kocl_sync_call_data_cache;

/*
 * Async GPU call.
 */
int kocl_offload_async(struct kocl_request *req)
{
    struct _kocl_request_item *item;
    
    if (unlikely(kocldev.state == KOCL_TERMINATED)) {
	kocl_log(KOCL_LOG_ALERT,
		 "kocl is terminated, no request accepted any more\n");
	return KOCL_TERMINATED;
    }
    
    item = kmem_cache_alloc(kocl_request_item_cache, GFP_KERNEL);

    if (!item) {
	kocl_log(KOCL_LOG_ERROR, "out of memory for kocl request\n");
	return -ENOMEM;
    }
    item->r = req;
    
    spin_lock(&(kocldev.reqlock));

    INIT_LIST_HEAD(&item->list);
    list_add_tail(&item->list, &(kocldev.reqs));
    
    wake_up_interruptible(&(kocldev.reqq));
    
    spin_unlock(&(kocldev.reqlock));

   
    
    return 0;
}
EXPORT_SYMBOL_GPL(kocl_offload_async);

/*
 * Callback for sync GPU call.
 */
static int sync_callback(struct kocl_request *req)
{
    struct _kocl_sync_call_data *data = (struct _kocl_sync_call_data*)
	req->kdata;
    
    data->done = 1;
    
    wake_up_interruptible(&data->queue);
    
    return 0;
}

/*
 * Sync GPU call
 */
int kocl_offload_sync(struct kocl_request *req)
{
    struct _kocl_sync_call_data *data;
    struct _kocl_request_item *item;

    if (unlikely(kocldev.state == KOCL_TERMINATED)) {
	kocl_log(KOCL_LOG_ALERT,
		 "kocl is terminated, no request accepted any more\n");
	return KOCL_TERMINATED;
    }
    
    data = kmem_cache_alloc(kocl_sync_call_data_cache, GFP_KERNEL);
    if (!data) {
	kocl_log(KOCL_LOG_ERROR, "kocl_call_sync alloc mem failed\n");
	return -ENOMEM;
    }
    item = kmem_cache_alloc(kocl_request_item_cache, GFP_KERNEL);
    if (!item) {
	kocl_log(KOCL_LOG_ERROR, "out of memory for kocl request\n");
	return -ENOMEM;
    }
    item->r = req;
    
    data->oldkdata = req->kdata;
    data->oldcallback = req->callback;
    data->done = 0;
    init_waitqueue_head(&data->queue);
    
    req->kdata = data;
    req->callback = sync_callback;
    
    spin_lock(&(kocldev.reqlock));

    INIT_LIST_HEAD(&item->list);//初始化list node, prev,next 都指向自己 
    list_add_tail(&item->list, &(kocldev.reqs));//把item->list node 加入到 reqs list 裡面（先進先出）

    wake_up_interruptible(&(kocldev.reqq));//有收到reqs 把在kocl_read() reqq queue的process 叫醒
    
    spin_unlock(&(kocldev.reqlock));

    //process先在data queue等,如果kocl_wrte()收到reqs回來則會呼叫sync_callback 
    wait_event_interruptible(data->queue, (data->done==1));
        
    req->kdata = data->oldkdata;
    req->callback = data->oldcallback;
 
    kmem_cache_free(kocl_sync_call_data_cache, data);
    return 0;
}
EXPORT_SYMBOL_GPL(kocl_offload_sync);


int kocl_next_request_id(void)
{
    int rt = -1;
    
    spin_lock(&(kocldev.ridlock));
    
    kocldev.rid_sequence++;
    if (kocldev.rid_sequence < 0)
	kocldev.rid_sequence = 0;
    rt = kocldev.rid_sequence;
    
    spin_unlock(&(kocldev.ridlock));

    return rt;
}
EXPORT_SYMBOL_GPL(kocl_next_request_id);

static void kocl_request_item_constructor(void *data)
{
    struct _kocl_request_item *item =
	(struct _kocl_request_item*)data;

    if (item) {
	memset(item, 0, sizeof(struct _kocl_request_item));
	INIT_LIST_HEAD(&item->list);
	item->r = NULL;
    }
}

static void kocl_sync_call_data_constructor(void *data)
{
    struct _kocl_sync_call_data *d =
	(struct _kocl_sync_call_data*)data;
    if (d) {
	memset(d, 0, sizeof(struct _kocl_sync_call_data));
    }
}

static void kocl_request_constructor(void* data)
{
    struct kocl_request *req = (struct kocl_request*)data;
    if (req) {
	memset(req, 0, sizeof(struct kocl_request));
	req->id = kocl_next_request_id();
	req->service_name[0] = 0;
    }
}

struct kocl_request* kocl_alloc_request(void)
{
    struct kocl_request *req =
	kmem_cache_alloc(kocl_request_cache, GFP_KERNEL);
    return req;
}
EXPORT_SYMBOL_GPL(kocl_alloc_request);


void kocl_free_request(struct kocl_request* req)
{
    kmem_cache_free(kocl_request_cache, req);
}
EXPORT_SYMBOL_GPL(kocl_free_request);


void* kocl_malloc(unsigned long nbytes, int channel)
{
    unsigned int req_nunits = DIV_ROUND_UP(nbytes, KOCL_BUF_UNIT_SIZE);
    void *p = NULL;
    unsigned long idx=0UL;
    unsigned long *bitmap=NULL;
    u32 nunits=0;
    unsigned long kva=0UL;
    u32 *alloc_sz=NULL;

    spin_lock(&kocldev.gmpool_lock);
         if(channel==2){      
             bitmap = kocldev.gmpool2.bitmap;
             nunits = kocldev.gmpool2.nunits;
                kva = kocldev.gmpool2.kva;
           alloc_sz = kocldev.gmpool2.alloc_sz;
        }else if(channel==3){
             bitmap = kocldev.gmpool3.bitmap;
             nunits = kocldev.gmpool3.nunits;
                kva = kocldev.gmpool3.kva;
           alloc_sz = kocldev.gmpool3.alloc_sz;
        }else{
            //channel 0 or 1 
             bitmap = kocldev.gmpool.bitmap;
             nunits = kocldev.gmpool.nunits;
                kva = kocldev.gmpool.kva;
           alloc_sz = kocldev.gmpool.alloc_sz;
        }

    idx = bitmap_find_next_zero_area(bitmap, nunits, 0, req_nunits, 0);
    if (idx < nunits) {
	    bitmap_set(bitmap, idx, req_nunits);
	    p = (void*)((unsigned long)(kva) + idx*KOCL_BUF_UNIT_SIZE);
	    alloc_sz[idx] = req_nunits;
    // kocl_log(KOCL_LOG_PRINT, "idx:%lu gmpool.nunits:%d req_nunits:%d p:%p \n",idx, kocldev.gmpool.nunits, req_nunits, p);
    
    } else {
	    kocl_log(KOCL_LOG_ERROR, "idx:%lu 20:%d\n", idx, nunits);
	    kocl_log(KOCL_LOG_ERROR, "out of GPU memory for malloc %lu\n",  nbytes);
    }
    spin_unlock(&kocldev.gmpool_lock);
    return p;	
}
EXPORT_SYMBOL_GPL(kocl_malloc);

void kocl_free(void *p, int channel)
{  
    unsigned long idx=0UL;
    unsigned int alloc_nunits=0;
    unsigned long *bitmap=NULL;
    u32 nunits=0;
    unsigned long kva=0UL;
    u32 *alloc_sz=NULL;

        if(channel==2){      
             bitmap = kocldev.gmpool2.bitmap;
             nunits = kocldev.gmpool2.nunits;
                kva = kocldev.gmpool2.kva;
           alloc_sz = kocldev.gmpool2.alloc_sz;

        }else if(channel==3){
             bitmap = kocldev.gmpool3.bitmap;
             nunits = kocldev.gmpool3.nunits;
                kva = kocldev.gmpool3.kva;
           alloc_sz = kocldev.gmpool3.alloc_sz;
        }else{
            //channel 0 or 1 
             bitmap = kocldev.gmpool.bitmap;
             nunits = kocldev.gmpool.nunits;
                kva = kocldev.gmpool.kva;
           alloc_sz = kocldev.gmpool.alloc_sz;
        }

     idx =	((unsigned long)(p)-(unsigned long)(kva))/KOCL_BUF_UNIT_SIZE;
  
    if (idx < 0 || idx >= nunits) {
	     kocl_log(KOCL_LOG_ERROR, "incorrect GPU memory pointer 0x%lX to free\n",  p);
	    return;
    }
    alloc_nunits = alloc_sz[idx];
    if (alloc_nunits == 0) {
	/*
	 * We allow such case because this allows users free memory
	 * from any field among in, out and data in request.
	 */
	    return;
    }
    if (alloc_nunits > (nunits - idx)) {
	    kocl_log(KOCL_LOG_ERROR, "incorrect GPU memory allocation info: "
		        "allocated %u units at unit index %u\n", alloc_nunits, idx);
	    return;
    }

    spin_lock(&kocldev.gmpool_lock);    
  // kocl_log(KOCL_LOG_PRINT, "pool2:idx:%lu nunits:%d  p:%p \n", idx, nunits, p);
    bitmap_clear(bitmap, idx, alloc_nunits);
    alloc_sz[idx] = 0;
    spin_unlock(&kocldev.gmpool_lock);

}
EXPORT_SYMBOL_GPL(kocl_free);

/*
 * find request by id in the rtdreqs
 * offlist = 1: remove the request from the list
 * offlist = 0: keep the request in the list
 */
static struct _kocl_request_item* find_request(int id, int offlist)
{
    struct _kocl_request_item *pos, *n;

    spin_lock(&(kocldev.rtdreqlock));
    
    list_for_each_entry_safe(pos, n, &(kocldev.rtdreqs), list) {
	if (pos->r->id == id) {
	    if (offlist)
		list_del(&pos->list);
	    spin_unlock(&(kocldev.rtdreqlock));
	    return pos;
	}
    }

    spin_unlock(&(kocldev.rtdreqlock));

    return NULL;
}


int kocl_open(struct inode *inode, struct file *filp)
{
    if (!atomic_dec_and_test(&kocldev_av)) { //atomic_dec_and_test(atomic_t *v) 為遞減v的值並且測試運算結果
	atomic_inc(&kocldev_av);//atomic_inc(* v) 遞減v的值
	return -EBUSY;
    }

    filp->private_data = &kocldev;
    return 0;
}

int kocl_release(struct inode *inode, struct file *file)
{
    atomic_set(&kocldev_av, 1);
    return 0;
}

static void fill_ku_request(struct kocl_ku_request *kureq,
			   struct kocl_request *req)
{
    unsigned long uva=0UL;    
    unsigned long kva=0UL;
    u32 npages=0; 

    kureq->id = req->id;
    memcpy(kureq->service_name, req->service_name, KOCL_SERVICE_NAME_SIZE);

        if(req->channel==2){
                kva = kocldev.gmpool2.kva ;
                uva = kocldev.gmpool2.uva ;
             npages = kocldev.gmpool2.npages ;
   
        }else if(req->channel==3){
                kva = kocldev.gmpool3.kva ;
                uva = kocldev.gmpool3.uva ;
             npages = kocldev.gmpool3.npages ;
        }else{
            //channel 0 or 1
                kva = kocldev.gmpool.kva ;
                uva = kocldev.gmpool.uva ;
             npages = kocldev.gmpool.npages ;
        }

    //ADDR_REBASE(dst_base,src_base,src_pointer),//= dst_base+(src_pointer-src_base)
    if (ADDR_WITHIN(req->in, kva, npages<<PAGE_SHIFT)) {
	    kureq->in = (void*)ADDR_REBASE( uva, kva, req->in);
    } else {
	    kureq->in = req->in;
    }

    if (ADDR_WITHIN(req->out, kva, npages<<PAGE_SHIFT)) {
	    kureq->out = (void*)ADDR_REBASE( uva, kva, req->out);
    } else {
	    kureq->out = req->out;
    }

    if (ADDR_WITHIN(req->udata, kva, npages<<PAGE_SHIFT)) {
	    kureq->data = (void*)ADDR_REBASE( uva, kva, req->udata);
    } else {
	    kureq->data = req->udata;
    } 

    kureq->channel= req->channel;
    kureq->insize = req->insize;
    kureq->outsize = req->outsize;
    kureq->datasize = req->udatasize;
}

ssize_t kocl_read(
    struct file *filp, char __user *buf, size_t c, loff_t *fpos)
{
    ssize_t ret = 0;
    struct list_head *r;
    struct _kocl_request_item *item;

    spin_lock(&(kocldev.reqlock));
    while (list_empty(&(kocldev.reqs))) {//這邊會去看reqs list 是否為空,如果為空傳回一個非0值表示沒有
	spin_unlock(&(kocldev.reqlock));

	if (filp->f_flags & O_NONBLOCK)
	    return -EAGAIN;

	if (wait_event_interruptible(
		kocldev.reqq, (!list_empty(&(kocldev.reqs)))))//如果kocl_call_sync()沒有收到reqs,process 在reqq queue等 
	    return -ERESTARTSYS;
	spin_lock(&(kocldev.reqlock));
    }
 
    r = kocldev.reqs.next;//找reqs list head 下一個node
    list_del(r);//取出node
    item = list_entry(r, struct _kocl_request_item, list);//藉由 r 算出kocl_request_item 結構的初始位置,有點複雜QQ 看圖
    if (item) {
	struct kocl_ku_request kureq;
	fill_ku_request(&kureq, item->r);//算出位址得到item後把在kocl收到request的資訊(kocl_request *)給kureq
	
	/*memcpy*/copy_to_user(buf, &kureq, sizeof(struct kocl_ku_request));//再把kureq 的資訊給userspace helper的buf
	ret = c;
    }

    spin_unlock(&(kocldev.reqlock));

    if (ret > 0 && item) {
	spin_lock(&(kocldev.rtdreqlock));

	INIT_LIST_HEAD(&item->list);
	list_add_tail(&item->list, &(kocldev.rtdreqs));//這時候把item加入到rtdreqs list 給kocl_wrte()查此item

	spin_unlock(&(kocldev.rtdreqlock));
    }
    
    *fpos += ret;

    return ret;    
}

ssize_t kocl_write(struct file *filp, const char __user *buf,
		   size_t count, loff_t *fpos)
{
    struct kocl_ku_response kuresp;
    struct _kocl_request_item *item;
    ssize_t ret = 0;
    size_t  realcount;
    
    if (count < sizeof(struct kocl_ku_response))
	ret = -EINVAL; /* Too small. */
    else
    {
	realcount = sizeof(struct kocl_ku_response);

	/*memcpy*/copy_from_user(&kuresp, buf, realcount);//把userspace helper傳來的response buf給kuresp

	item = find_request(kuresp.id, 1);//用原本送出去的reqs id 從rtdreqs list去找 
	if (!item)
	{	    
	    ret = -EFAULT; /* no request found */
	} else {
	    item->r->errcode = kuresp.errcode;
	    if (unlikely(kuresp.errcode != 0)) {
		switch(kuresp.errcode) {
		case KOCL_NO_RESPONSE:
		    kocl_log(KOCL_LOG_ALERT,
			"userspace helper doesn't give any response\n");
		    break;
		case KOCL_NO_SERVICE:
		    kocl_log(KOCL_LOG_ALERT,
			     "no such service %s\n",
			     item->r->service_name);
		    break;
		case KOCL_TERMINATED:
		    kocl_log(KOCL_LOG_ALERT,
			     "request is terminated\n"
			);
		    break;
		default:
		    kocl_log(KOCL_LOG_ALERT,
			     "unknown error with code %d\n",
			     kuresp.id);
		    break;		    
		}
	    }

	    /*
	     * Different strategy should be applied here:
	     * #1 invoke the callback in the write syscall, like here.
	     * #2 add the resp into the resp-list in the write syscall
	     *    and return, a kernel thread will process the list
	     *    and invoke the callback.
	     *
	     * Currently, the first one is used because this can ensure
	     * the fast response. A kthread may have to sleep so that
	     * the response can't be processed ASAP.
	     */
	    item->r->callback(item->r);
	    ret = count;/*realcount;*/
	    *fpos += ret;
	    kmem_cache_free(kocl_request_item_cache, item);
	}
    }

    return ret;
}

static int clear_gpu_mempool(void)
{
    struct _kocl_mempool *gmp = &kocldev.gmpool;
    struct _kocl_mempool *gmp2 = &kocldev.gmpool2;
    struct _kocl_mempool *gmp3 = &kocldev.gmpool3;
    int i;

    spin_lock(&kocldev.gmpool_lock);

    for (i=0; i<gmp->npages; i++){
        if (!PageReserved(gmp->pages[i]))
             SetPageDirty(gmp->pages[i]);
	      put_page(gmp->pages[i]);
	};	
    if (gmp->pages)
	kfree(gmp->pages);
    if (gmp->bitmap)
	kfree(gmp->bitmap);
    if (gmp->alloc_sz)
	kfree(gmp->alloc_sz);
    vunmap((void*)gmp->kva);

    for (i=0; i<gmp2->npages; i++){
        if (!PageReserved(gmp2->pages[i]))
             SetPageDirty(gmp2->pages[i]);
	      put_page(gmp2->pages[i]);
	};
    if (gmp2->pages)
	kfree(gmp2->pages);
    if (gmp2->bitmap)
	kfree(gmp2->bitmap);
    if (gmp2->alloc_sz)
	kfree(gmp2->alloc_sz);
    vunmap((void*)gmp2->kva);
    
    for (i=0; i<gmp3->npages; i++){
        if (!PageReserved(gmp3->pages[i]))
             SetPageDirty(gmp3->pages[i]);
	      put_page(gmp3->pages[i]);
	};
    if (gmp3->pages)
	kfree(gmp3->pages);
    if (gmp3->bitmap)
	kfree(gmp3->bitmap);
    if (gmp3->alloc_sz)
	kfree(gmp3->alloc_sz);
    vunmap((void*)gmp3->kva);


    spin_unlock(&kocldev.gmpool_lock);
    return 0;
}

static int set_gpu_mempool(char __user *buf)
{
    struct kocl_gpu_mem_info gb;
    struct _kocl_mempool *gmp = &kocldev.gmpool;
    struct _kocl_mempool *gmp2 = &kocldev.gmpool2;
    struct _kocl_mempool *gmp3 = &kocldev.gmpool3;
    int rt;
    int err=0;
   
    spin_lock(&(kocldev.gmpool_lock));    
     
    copy_from_user(&gb, buf, sizeof(struct kocl_gpu_mem_info));//把helper的pinned memory(hostbuf.uva)給gb

kocl_log(KOCL_LOG_PRINT, "gmp->uva1: %p \n",gb.uva);
kocl_log(KOCL_LOG_PRINT, "gmp->uva2: %p \n",gb.uva2);
kocl_log(KOCL_LOG_PRINT, "gmp->uva3: %p \n",gb.uva3);

    /* set up pages mem */
    gmp->uva = (unsigned long)(gb.uva);
    gmp2->uva = (unsigned long)(gb.uva2);
    gmp3->uva = (unsigned long)(gb.uva3);

    gmp->npages = gb.size/PAGE_SIZE;// 1G/4K = 262144,共有 2^18個pages
    gmp2->npages = gb.size/PAGE_SIZE;// 1G/4K = 262144,共有 2^18個pages
    gmp3->npages = gb.size/PAGE_SIZE;

    if (!gmp->pages) {
	gmp->pages = kmalloc(sizeof(struct page*)*gmp->npages, GFP_KERNEL);//配置262144(1024*256)個page pointer array space 
	if (!gmp->pages) {
	    kocl_log(KOCL_LOG_ERROR, "run out of memory for gmp pages\n");
	    err = -ENOMEM;
	    goto unlock_and_out;
	  }
    }
    if (!gmp2->pages) {
	gmp2->pages = kmalloc(sizeof(struct page*)*gmp2->npages, GFP_KERNEL);//配置262144(1024*256)個page pointer array space 
	if (!gmp2->pages) {
	    kocl_log(KOCL_LOG_ERROR, "run out of memory for gmp pages\n");
	    err = -ENOMEM;
	    goto unlock_and_out;
	  }
    }
     if (!gmp3->pages) {
	gmp3->pages = kmalloc(sizeof(struct page*)*gmp3->npages, GFP_KERNEL);//配置262144(1024*256)個page pointer array space 
	if (!gmp3->pages) {
	    kocl_log(KOCL_LOG_ERROR, "run out of memory for gmp pages\n");
	    err = -ENOMEM;
	    goto unlock_and_out;
	  }
    }

    /* for Linux kernel 4.4.0 version below works
    down_read(&current->mm->mmap_sem);
        rt = get_user_pages(current, current->mm, gmp->uva, gmp->npages , 0 , 0 , gmp->pages, NULL);
    up_read(&current->mm->mmap_sem);*/
   
   /* for Linux Kernel 4.8.0/4.7.0 */
    down_read(&current->mm->mmap_sem);
        rt = get_user_pages( gmp->uva, gmp->npages , 0 , 0 , gmp->pages, NULL);
    up_read(&current->mm->mmap_sem);      
    if (rt<=0) {
	      kocl_log(KOCL_LOG_PRINT,"[kocl] DEBUG: no page pinned at pages2 %d\n", rt);
	    goto unlock_and_out;
    }
    
    down_read(&current->mm->mmap_sem);
        rt = get_user_pages( gmp2->uva, gmp2->npages , 0 , 0 , gmp2->pages, NULL);
    up_read(&current->mm->mmap_sem);        
    if (rt<=0) {
	      kocl_log(KOCL_LOG_PRINT,"[kocl] DEBUG: no page pinned at pages2 %d\n", rt);
	    goto unlock_and_out;
    }

    down_read(&current->mm->mmap_sem);
        rt = get_user_pages( gmp3->uva, gmp3->npages , 0 , 0 , gmp3->pages, NULL);
    up_read(&current->mm->mmap_sem);        
    if (rt<=0) {
	      kocl_log(KOCL_LOG_PRINT,"[kocl] DEBUG: no page pinned at pages2 %d\n", rt);
	    goto unlock_and_out;
    }

    /* set up bitmap */
    gmp->nunits = gmp->npages/KOCL_BUF_NR_FRAMES_PER_UNIT;// 262144(1024*256)/256 = 1024 個nunits
    gmp2->nunits = gmp2->npages/KOCL_BUF_NR_FRAMES_PER_UNIT;// 262144(1024*256)/256 = 1024 個nunits
    gmp3->nunits = gmp3->npages/KOCL_BUF_NR_FRAMES_PER_UNIT;

kocl_log(KOCL_LOG_PRINT, "pool1: nunits=%d npage=%d KOCL_BUF=%d\n",
             gmp->nunits,gmp->npages,KOCL_BUF_NR_FRAMES_PER_UNIT);
kocl_log(KOCL_LOG_PRINT, "pool2: nunits=%d npage=%d KOCL_BUF=%d\n",
             gmp2->nunits,gmp2->npages,KOCL_BUF_NR_FRAMES_PER_UNIT);
kocl_log(KOCL_LOG_PRINT, "pool3: nunits=%d npage=%d KOCL_BUF=%d\n",
             gmp3->nunits,gmp3->npages,KOCL_BUF_NR_FRAMES_PER_UNIT);               

    if (!gmp->bitmap) {
	gmp->bitmap = kmalloc(
	    BITS_TO_LONGS(gmp->nunits)*sizeof(long), GFP_KERNEL);//配置bitmap 所需要的空間long 長度8, 1024有16個 
	    if (!gmp->bitmap) {
	        kocl_log(KOCL_LOG_PRINT , "run out of memory for gmp bitmap\n");
	        err = -ENOMEM;
	        goto unlock_and_out;
	    }
    }    
    bitmap_zero(gmp->bitmap, gmp->nunits);//把bitmap array 初始化清成 0

    if (!gmp2->bitmap) {
	gmp2->bitmap = kmalloc(
	    BITS_TO_LONGS(gmp2->nunits)*sizeof(long), GFP_KERNEL);//配置bitmap 所需要的空間long 長度8, 1024有16個 
	    if (!gmp2->bitmap) {
	        kocl_log(KOCL_LOG_PRINT , "run out of memory for gmp bitmap\n");
	        err = -ENOMEM;
	        goto unlock_and_out;
	    }
    }    
    bitmap_zero(gmp2->bitmap, gmp2->nunits);//把bitmap array 初始化清成 0

    if (!gmp3->bitmap) {
	gmp3->bitmap = kmalloc(
	    BITS_TO_LONGS(gmp3->nunits)*sizeof(long), GFP_KERNEL);//配置bitmap 所需要的空間long 長度8, 1024有16個 
	    if (!gmp3->bitmap) {
	        kocl_log(KOCL_LOG_PRINT , "run out of memory for gmp bitmap\n");
	        err = -ENOMEM;
	        goto unlock_and_out;
	    }
    }    
    bitmap_zero(gmp3->bitmap, gmp3->nunits);//把bitmap array 初始化清成 0


    /* set up allocated memory sizes */ //alloc_sz 主要是看已經分配多少req_nunits空間
    if (!gmp->alloc_sz) {
	gmp->alloc_sz = kmalloc(
	    gmp->nunits*sizeof(u32), GFP_KERNEL);
	    if (!gmp->alloc_sz) {
	        kocl_log(KOCL_LOG_ERROR,
		        "run out of memory for gmp alloc_sz\n");
	        err = -ENOMEM;
	        goto unlock_and_out;
	    }
    }
    memset(gmp->alloc_sz, 0, gmp->nunits);//把alloc_sz array 初始化清成 0

    if (!gmp2->alloc_sz) {
	gmp2->alloc_sz = kmalloc(
	    gmp2->nunits*sizeof(u32), GFP_KERNEL);
	    if (!gmp2->alloc_sz) {
	        kocl_log(KOCL_LOG_ERROR,
		        "run out of memory for gmp alloc_sz\n");
	        err = -ENOMEM;
	        goto unlock_and_out;
	    }
    }
    memset(gmp2->alloc_sz, 0, gmp2->nunits);//把alloc_sz array 初始化清成 0

    if (!gmp3->alloc_sz) {
	gmp3->alloc_sz = kmalloc(
	    gmp3->nunits*sizeof(u32), GFP_KERNEL);
	    if (!gmp3->alloc_sz) {
	        kocl_log(KOCL_LOG_ERROR,
		        "run out of memory for gmp alloc_sz\n");
	        err = -ENOMEM;
	        goto unlock_and_out;
	    }
    }
    memset(gmp3->alloc_sz, 0, gmp3->nunits);//把alloc_sz array 初始化清成 0

    /* set up kernel remapping *///把helper的meomory page map 到kernel 
    gmp->kva = (unsigned long)vmap(gmp->pages, gmp->npages, GFP_KERNEL, PAGE_KERNEL);
    gmp2->kva = (unsigned long)vmap(gmp2->pages, gmp2->npages, GFP_KERNEL, PAGE_KERNEL);
    gmp3->kva = (unsigned long)vmap(gmp3->pages, gmp3->npages, GFP_KERNEL, PAGE_KERNEL);

    if (!gmp->kva) {
	    kocl_log(KOCL_LOG_ERROR, "map pages into kernel failed\n");
	    err = -EFAULT;
	    goto unlock_and_out;
    }
    if (!gmp2->kva) {
	    kocl_log(KOCL_LOG_ERROR, "map pages into kernel failed\n");
	    err = -EFAULT;
	    goto unlock_and_out;
    }
    if (!gmp3->kva) {
	    kocl_log(KOCL_LOG_ERROR, "map pages into kernel failed\n");
	    err = -EFAULT;
	    goto unlock_and_out;
    }

    kocl_log(KOCL_LOG_PRINT, "gmp->kva: %p \n",gmp->kva);
    kocl_log(KOCL_LOG_PRINT, "gmp2->kva: %p \n",gmp2->kva);
    kocl_log(KOCL_LOG_PRINT, "gmp3->kva: %p \n",gmp3->kva);


unlock_and_out:
    spin_unlock(&(kocldev.gmpool_lock));    

    return err;
}

static int dump_gpu_bufs(char __user *buf)
{
    /* TODO: dump gmpool's info to buf */
    return 0;
}

static int terminate_all_requests(void)
{
    /* TODO: stop receiving requests, set all reqeusts code to
     kocl_TERMINATED and call their callbacks */
    kocldev.state = KOCL_TERMINATED;
    return 0;
}

static long kocl_ioctl(struct file *filp,
	       unsigned int cmd, unsigned long arg)
{
    int err = 0;
    
    if (_IOC_TYPE(cmd) != KOCL_IOC_MAGIC)
	return -ENOTTY;
    if (_IOC_NR(cmd) > KOCL_IOC_MAXNR) return -ENOTTY;

    if (_IOC_DIR(cmd) & _IOC_READ)
	err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
    else if (_IOC_DIR(cmd) & _IOC_WRITE)
	err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    if (err) return -EFAULT;

    switch (cmd) {
	
    case KOCL_IOC_SET_GPU_BUFS:
	err = set_gpu_mempool((char*)arg);
	break;
	
    case KOCL_IOC_GET_GPU_BUFS:
	err = dump_gpu_bufs((char*)arg);
	break;

    case KOCL_IOC_SET_STOP:
	err = terminate_all_requests();
	break;

    default:
	err = -ENOTTY;
	break;
    }

    return err;
}

static unsigned int kocl_poll(struct file *filp, poll_table *wait)
{
    unsigned int mask = 0;
    
    spin_lock(&(kocldev.reqlock));
    
    poll_wait(filp, &(kocldev.reqq), wait);//先在reqq sleep

    if (!list_empty(&(kocldev.reqs))) 
	mask |= POLLIN | POLLRDNORM;//可讀取

    mask |= POLLOUT | POLLWRNORM;//可寫入

    spin_unlock(&(kocldev.reqlock));

    return mask;
}


static struct file_operations kocl_ops =  {
    .owner          = THIS_MODULE,
    .read           = kocl_read,
    .write          = kocl_write,
    .poll           = kocl_poll,
    .unlocked_ioctl = kocl_ioctl,
    .open           = kocl_open,
    .release        = kocl_release,   
};

/* for ksm  */
struct fordedup dedup = {
         .kkocl_alloc_request    = kocl_alloc_request,
         .kkocl_malloc          = kocl_malloc, 
         .kkocl_offload_sync        = kocl_offload_sync,
         .kkocl_offload_async       = kocl_offload_async,
         .kkocl_next_request_id  = kocl_next_request_id,
	     .kkocl_free_request     = kocl_free_request,
	     .kkocl_free            = kocl_free,
};


static int kocl_init(void)
{
    
    int result = 0;
    int devno;
  
    printk("dedup:%p",&dedup); 
  
    kocldev.state = KOCL_OK;
    
    INIT_LIST_HEAD(&(kocldev.reqs));
    INIT_LIST_HEAD(&(kocldev.rtdreqs));
    
    spin_lock_init(&(kocldev.reqlock));
    spin_lock_init(&(kocldev.rtdreqlock));

    init_waitqueue_head(&(kocldev.reqq));

    spin_lock_init(&(kocldev.ridlock));
    spin_lock_init(&(kocldev.gmpool_lock));
    

       
    kocldev.rid_sequence = 0;

    kocl_request_cache = kmem_cache_create(
	"kocl_request_cache", sizeof(struct kocl_request), 0,
	SLAB_HWCACHE_ALIGN, kocl_request_constructor);
    if (!kocl_request_cache) {
	kocl_log(KOCL_LOG_ERROR, "can't create request cache\n");
	return -EFAULT;
    }
    
    kocl_request_item_cache = kmem_cache_create(
	"kocl_request_item_cache", sizeof(struct _kocl_request_item), 0,
	SLAB_HWCACHE_ALIGN, kocl_request_item_constructor);
    if (!kocl_request_item_cache) {
	kocl_log(KOCL_LOG_ERROR, "can't create request item cache\n");
	kmem_cache_destroy(kocl_request_cache);
	return -EFAULT;
    }

    kocl_sync_call_data_cache = kmem_cache_create(
	"kocl_sync_call_data_cache", sizeof(struct _kocl_sync_call_data), 0,
	SLAB_HWCACHE_ALIGN, kocl_sync_call_data_constructor);
    if (!kocl_sync_call_data_cache) {
	kocl_log(KOCL_LOG_ERROR, "can't create sync call data cache\n");
	kmem_cache_destroy(kocl_request_cache);
	kmem_cache_destroy(kocl_request_item_cache);
	return -EFAULT;
    }
    
    /* initialize buffer info */
    memset(&kocldev.gmpool, 0, sizeof(struct _kocl_mempool));   

    /* alloc dev */	
    result = alloc_chrdev_region(&kocldev.devno, 0, 1 , KOCL_DEV_NAME);//動態取得 major number
    devno = MAJOR(kocldev.devno);//把主編號切出來給devno
    kocldev.devno = MKDEV(devno, 0);//再由MKDEV合成裝置編號

    if (result < 0) {
        kocl_log(KOCL_LOG_ERROR, "can't get major\n");
    } else {
	struct device *device;
	memset(&kocldev.cdev, 0, sizeof(struct cdev));
	cdev_init(&kocldev.cdev, &kocl_ops);//豋記系統呼叫 handler
	kocldev.cdev.owner = THIS_MODULE;
	kocldev.cdev.ops = &kocl_ops;
	result = cdev_add(&kocldev.cdev, kocldev.devno, 1);
	if (result) {
	    kocl_log(KOCL_LOG_ERROR, "can't add device %d", result);
	}
    
     /* dev class */
    kocldev.cls = class_create(THIS_MODULE, "KOCL_DEV_NAME");
    if (IS_ERR(kocldev.cls)) {
	result = PTR_ERR(kocldev.cls);
	kocl_log(KOCL_LOG_ERROR, "can't create dev class for KOCL\n");
	return result;
    }

	/* create /dev/kocl */
	device = device_create(kocldev.cls, NULL, kocldev.devno, NULL,
			       KOCL_DEV_NAME);
	if (IS_ERR(device)) {
	    kocl_log(KOCL_LOG_ERROR, "creating device failed\n");
	    result = PTR_ERR(device);
	  }
   }

    return result;
}

static void kocl_cleanup(void)
{
    kocldev.state = KOCL_TERMINATED;

    device_destroy(kocldev.cls, kocldev.devno);
    cdev_del(&kocldev.cdev);
    class_destroy(kocldev.cls);

    unregister_chrdev_region(kocldev.devno, 1);
    if (kocl_request_cache)
	kmem_cache_destroy(kocl_request_cache);
    if (kocl_request_item_cache)
	kmem_cache_destroy(kocl_request_item_cache);
    if (kocl_sync_call_data_cache)
	kmem_cache_destroy(kocl_sync_call_data_cache);

    clear_gpu_mempool();
   
}

static int __init mod_init(void)
{
    kocl_log(KOCL_LOG_PRINT, "KOCL loaded\n");
    return kocl_init();
}

static void __exit mod_exit(void)
{
    kocl_cleanup();
    kocl_log(KOCL_LOG_PRINT, "KOCL unloaded\n");
}

module_init(mod_init);
module_exit(mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Weibin Sun");
MODULE_DESCRIPTION("GPU computing framework and driver");
