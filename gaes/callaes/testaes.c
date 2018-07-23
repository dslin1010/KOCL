/* -*- linux-c -*-
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the GPL-COPYING file in the top-level directory.
 *
 * Copyright (c) 2010-2011 University of Utah and the Flux Group.
 * All rights reserved.
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


char* AES_GENERIC = "ecb(aes-generic)";
char* AES_ASM = "ecb(aes-asm)";
char* AES = "ecb(aes)";

char* AES_GPU_GENERIC = "gaes_ecb(aes-generic)";
char* AES_GPU_ASM = "gaes_ecb(aes-asm)";
char* AES_GPU = "gaes_ecb(aes)";

char* CIPHER;

#define MAX_BLK_SIZE (32*1024*1024)
#define MIN_BLK_SIZE (4*1024)

#define TEST_TIMES 1

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


void test_aes(void)
{
	struct crypto_blkcipher *tfm;
	struct blkcipher_desc desc;	
	int i,j;
    unsigned int ret;    	
    u8 *iv;
    
    struct scatterlist sg[3];
    u8 encrypted[100];
    u8 decrypted[100];

    u8 aes_in[]={0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
	
	
	u8 key[] = {0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0x0F, 0x10, 0x11, 0x12,
	0x00, 0x01, 0x02, 0x03, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C, 0x0D, 0x0F, 0x10, 0x11, 0x12};	
    
	iv = kmalloc(32, GFP_KERNEL);
	if (!iv) {
	    printk("taes Error: failed to alloc IV\n");
	    return;
	}
	
	tfm = crypto_alloc_blkcipher(CIPHER, 0, 0);//allocate synchronous block cipher handle
	
	if (IS_ERR(tfm)) {
		printk("failed to load transform for %s: %ld\n", CIPHER,
			PTR_ERR(tfm));
		goto out;
	}
	desc.tfm = tfm;
	desc.flags = 0;
	desc.info = iv;

	ret = crypto_blkcipher_setkey(tfm, key, 16);//這邊只要選好cipher它會對應呼叫到gaes的setkey
	if (ret) {
		printk("setkey() failed flags=%x %lu\n",
				crypto_blkcipher_get_flags(tfm), sizeof(key));
	 	goto out;
	}
    
    sg_init_one(&sg[0], aes_in, 16);
    sg_init_one(&sg[1], encrypted, 16);
    sg_init_one(&sg[2], decrypted, 16);
    
    printk("\n "); 
    for(j=0;j<16;++j){
       printk(" %x ",aes_in[j]);
    }
    printk("\n "); 
    ret = crypto_blkcipher_encrypt_iv(&desc, &sg[1], &sg[0], 16);//crypto_blkcipher_encrypt_iv(dese, ciphertext, plaintext, nbytess)
			if (ret) {
				printk("taes ERROR: enc error\n");
				goto out;
            }

    printk(" Encrypted: ");
    for(j=0;j<16;++j){
       printk(" %x ",encrypted[j]);
    }
    
    ret = crypto_blkcipher_decrypt_iv(&desc, &sg[2], &sg[1], 16);//crypto_blkcipher_encrypt_iv(dese, ciphertext, plaintext, nbytess)
			if (ret) {
				printk("taes ERROR: enc error\n");
				goto out;
            }
    printk("\n ");        
    printk(" Decrypted: ");
    for(j=0;j<16;++j){
       printk(" %x ",decrypted[j]);
    }
   
out:    
	kfree(iv);
	crypto_free_blkcipher(tfm);	
}

static int __init taes_init(void)
{
	printk("test gaes loaded\n");	
	test_gpu = 1;	
	CIPHER = AES_GPU;//"gaes_ecb(aes)" 
    test_aes(); 
    test_gpu = 0;	
	//CIPHER = AES ;// "ecb(aes)" 
	//test_aes();
	
	return 0;
}

static void __exit taes_exit(void)
{
	printk("test gaes unloaded\n");
}

module_init(taes_init);
module_exit(taes_exit);

MODULE_DESCRIPTION("Test CUDA AES-ECB");
MODULE_LICENSE("GPL");

