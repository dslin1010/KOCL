/*
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the GPL-COPYING file in the top-level directory.
 *
 * Copyright (c) 2010-2011 University of Utah and the Flux Group.
 * All rights reserved.
 *
 * Copyright (c) 2017-2018 NCKU of Taiwan and the ASRLab.
 * Internal header used by KOCL only
 * 
 */

#ifndef ___KKOCL_H__
#define ___KKOCL_H__

#include "kocl.h"
#include <linux/types.h>

#define kocl_log(level, ...) kocl_do_log(level, "kocl", ##__VA_ARGS__)
#define dbg(...) kocl_log(KOCL_LOG_DEBUG, ##__VA_ARGS__)

/*
 * Buffer management stuff, put them here in case we may
 * create a kocl_buf.c for buffer related functions.
 */
#define KOCL_BUF_UNIT_SIZE (1024*1024)
#define KOCL_BUF_NR_FRAMES_PER_UNIT (KOCL_BUF_UNIT_SIZE/PAGE_SIZE)


#endif
