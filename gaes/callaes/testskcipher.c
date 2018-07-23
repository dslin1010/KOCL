/* -*- linux-c -*-
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the GPL-COPYING file in the top-level directory.
 *
 * Copyright (c) 2010-2011 University of Utah and the Flux Group.
 * All rights reserved.
 * 
 * Copyright (c) 2017-2018 NCKU of Taiwan and the ASRLab. 
 * 
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <linux/gfp.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/timex.h>
#include <linux/random.h>
#include <crypto/skcipher.h>


char* AES_GENERIC = "ecb(aes-generic)";
char* AES_ASM = "ecb(aes-asm)";
char* AES = "ecb(aes)";

char* AES_GPU_GENERIC = "gaes_ecb(aes-generic)";
char* AES_GPU_ASM = "gaes_ecb(aes-asm)";
char* AES_GPU = "gaes_ecb(aes)";

char* CIPHER;

#define MAX_BLK_SIZE (32*1024*1024)
#define MIN_BLK_SIZE (1*1024)

#define TEST_TIMES 10
#define ENC 1
#define DEC 0

int test_gpu = 0;
static int skip_cpu=0;
module_param(skip_cpu, int, 0444);
MODULE_PARM_DESC(skip_cpu, "do not test CPU cipher, default 0 (No)");

#if 0

static void dump_page_content(u8 *p)
{
    int r,c;
    printk("dump page content:\n");
    for (r=0; r<16; r++) {
	for (c=0; c<32; c++)
	    printk("%02x ", p[r*32+c]);
	printk("\n");
    }
}

static void dump_hex(u8 *p, int sz)
{
    int i;
    printk("dump hex:\n");
    for (i=0; i<sz; i++)
	printk("%02x ", p[i]);
    printk("\n");
}

#endif /* test only */

struct tcrypt_result {
    struct completion completion;
    int err;
};
/* tie all data structures together */
struct skcipher_def {
    struct scatterlist sg;
    struct crypto_skcipher *tfm;
    struct skcipher_request *req;
    struct tcrypt_result result;
};
/* Callback function */
static void test_skcipher_cb(struct crypto_async_request *req, int error)
{
    struct tcrypt_result *result = req->data;
   
    if (error == -EINPROGRESS)
        return;
    result->err = error;
    complete(&result->completion);
    printk("Encryption finished successfully\n");
}

/* Perform cipher operation */
static unsigned int test_skcipher_encdec(struct skcipher_def *sk,
                     int enc)
{
    int rc = 0;

    if (enc)
        rc = crypto_skcipher_encrypt(sk->req);
    else
        rc = crypto_skcipher_decrypt(sk->req);

    switch (rc) {
    case 0:
        break;
    case -EINPROGRESS://EINPROGRESS 36
    case -EBUSY://EBUSY 16
        rc = wait_for_completion_interruptible(
            &sk->result.completion);
        if (!rc && !sk->result.err) {
            reinit_completion(&sk->result.completion);
            break;
        }
    default:
        printk("skcipher encrypt returned with %d result %d\n",
            rc, sk->result.err);
        break;
    }
    init_completion(&sk->result.completion);

    return rc;
}

/* Initialize and trigger cipher operation */
static int test_skcipher(void)
{
    struct skcipher_def sk;
    struct crypto_skcipher *skcipher = NULL;
    struct skcipher_request *req = NULL;   
    char *ivdata = NULL;
    unsigned char key[16];
    int ret = -EFAULT;

    struct scatterlist *src;
	struct scatterlist *dst;
	char *buf, *buf2;
	char **ins, **outs;
    u32 bs;
	int i,j;
	u32 npages;
    struct timeval t0, t1;
	long int enc, dec;
    
    npages = MAX_BLK_SIZE/PAGE_SIZE;
   
    src = kmalloc(npages*sizeof(struct scatterlist), __GFP_ZERO|GFP_KERNEL);
	if (!src) {
		printk("taes ERROR: failed to alloc src\n");		
		return -1;
	}
	dst = kmalloc(npages*sizeof(struct scatterlist), __GFP_ZERO|GFP_KERNEL);
	if (!dst) {
		printk("taes ERROR: failed to alloc dst\n");
		kfree(src);		
		return -1;
	}
	ins = kmalloc(npages*sizeof(char*), __GFP_ZERO|GFP_KERNEL);
	if (!ins) {
		printk("taes ERROR: failed to alloc ins\n");
		kfree(src);
		kfree(dst);
		return -1;
	}
	outs = kmalloc(npages*sizeof(char*), __GFP_ZERO|GFP_KERNEL);
	if (!outs) {
		printk("taes ERROR: failed to alloc outs\n");
		kfree(src);
		kfree(dst);
		kfree(ins);		
		return -1;
	}
       
    skcipher = crypto_alloc_skcipher(CIPHER, 0, CRYPTO_ALG_ASYNC);//use sync cipher
    if (IS_ERR(skcipher)) {
        printk("could not allocate skcipher handle\n");
        return PTR_ERR(skcipher);
    }

    req = skcipher_request_alloc(skcipher, GFP_KERNEL);
    if (!req) {
        printk("could not allocate skcipher request\n");
        ret = -ENOMEM;
        goto out;
    }

    skcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
                      NULL,
                      NULL);

    /* AES 128 with random key */
    get_random_bytes(&key, 16);
    if (crypto_skcipher_setkey(skcipher, key, 16)) {
        printk("key could not be set\n");
        ret = -EAGAIN;
        goto out;
    }

    /* IV will be random */
    ivdata = kmalloc(16, GFP_KERNEL);
    if (!ivdata) {
        printk("could not allocate ivdata\n");
        goto out;
    }
    get_random_bytes(ivdata, 16);

    sk.tfm = skcipher;
    sk.req = req;
    
     sg_init_table(src, npages);//初始化scatterlist的entry
	for (i=0; i<npages; i++) {
		buf = (void *)__get_free_page(GFP_KERNEL);
		if (!buf) {
			printk("taes ERROR: alloc free page error\n");
			goto free_err_pages;
		}
		ins[i] = buf;		
		strcpy(buf, "this is a plain text!");
		sg_set_buf(src+i, buf, PAGE_SIZE);//設定sg裡面的entry 也就是sg->page_link會指到此buf page
		buf2 = (void *)__get_free_page(GFP_KERNEL);
		if (!buf2) {
			printk("taes ERROR: alloc free page error\n");
			goto free_err_pages;
		}
		outs[i] = buf2;
		sg_set_buf(dst+i, buf2, PAGE_SIZE);
	}

   
    for (bs = MIN_BLK_SIZE; bs <= MAX_BLK_SIZE; bs <<= 1) {
        
        skcipher_request_set_crypt(req, src, dst, bs, ivdata);
        init_completion(&sk.result.completion); 

        do_gettimeofday(&t0);
        for (j=0; j<TEST_TIMES; j++) {
            ret = test_skcipher_encdec(&sk, ENC);
            if (ret)
                goto out;
        }
        do_gettimeofday(&t1);
		enc = 1000000*(t1.tv_sec-t0.tv_sec) + 
			((int)(t1.tv_usec) - (int)(t0.tv_usec));    

        do_gettimeofday(&t0);
        for (j=0; j<TEST_TIMES; j++) {
            ret = test_skcipher_encdec(&sk, DEC);
            if (ret)
                goto out;
        }
        do_gettimeofday(&t1);
		dec = 1000000*(t1.tv_sec-t0.tv_sec) + 
			((int)(t1.tv_usec) - (int)(t0.tv_usec));  

        printk("%5s: Size %10u  enc time: %6ld us dec time: %6ld us\n",
		               test_gpu?"GAES":"CAES", bs, enc/TEST_TIMES, dec/TEST_TIMES);

    }


free_err_pages:
	for (i=0; i<npages && ins[i]; i++){		
		free_page((unsigned long)ins[i]);
	}
	for (i=0; i<npages && outs[i]; i++){
		free_page((unsigned long)outs[i]);
	}

out:
    kfree(src);
	kfree(dst);
	kfree(ins);
	kfree(outs);
    if (skcipher)
        crypto_free_skcipher(skcipher);
    if (req)
        skcipher_request_free(req);
    if (ivdata)
        kfree(ivdata);
   
    return ret;
}

static int __init taes_init(void)
{
	printk("test skcipher loaded\n");	
    test_gpu = 1;
    CIPHER ="gaes_ecb(aes)" ;
    test_skcipher(); 
    
   // test_gpu = 0;
   // CIPHER ="ecb(aes)" ;
   // test_skcipher(); 
   
	return 0;
}

static void __exit taes_exit(void)
{
	printk("test skcipher unloaded\n");
}

module_init(taes_init);
module_exit(taes_exit);

MODULE_DESCRIPTION("Test skcipher");
MODULE_LICENSE("GPL");

