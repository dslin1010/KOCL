/*
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the GPL-COPYING file in the top-level directory.
 *
 * Copyright (c) 2010-2011 University of Utah and the Flux Group.
 * All rights reserved.
 *
 * Copyright (c) 2017-2018 NCKU of Taiwan and the ASRLab.
 */
 
#ifndef __HELPER_H__
#define __HELPER_H__

#include "kocl.h"

#define kh_log(level, ...) kocl_do_log(level, "helper", ##__VA_ARGS__)
#define dbg(...) kh_log(KOCL_LOG_DEBUG, ##__VA_ARGS__)

extern struct kocl_gpu_mem_info hostbuf;
extern struct kocl_gpu_mem_info hostvma;
extern struct kocl_gpu_mem_info devbuf;
extern struct kocl_gpu_mem_info devbuf4vma;

#define __round_mask(x, y) ((__typeof__(x))((y)-1))
#define round_up(x, y) ((((x)-1) | __round_mask(x, y))+1)
#define round_down(x, y) ((x) & ~__round_mask(x, y))

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif



   
#endif
