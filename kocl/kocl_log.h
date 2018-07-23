/*
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the GPL-COPYING file in the top-level directory.
 *
 * Copyright (c) 2010-2011 University of Utah and the Flux Group.
 * All rights reserved.
 *
 * Copyright (c) 2017-2018 NCKU of Taiwan and the ASRLab.
 * 
 * Log functions
 */
#ifndef __KOCL_LOG_H__
#define __KOCL_LOG_H__

#define KOCL_LOG_INFO  1
#define KOCL_LOG_DEBUG 2
#define KOCL_LOG_ALERT 3
#define KOCL_LOG_ERROR 4
#define KOCL_LOG_PRINT 5

extern void kocl_generic_log(
    int level, const char *module, const char *filename,
    int lineno, const char *func, const char *fmt, ...);
extern int kocl_log_level;

#ifdef __KOCL_LOG_FULL_FILE_PATH__
  #define __FILE_NAME__ __FILE__
#else
  #ifdef __KERNEL__
    #include <linux/string.h>
  #else
    #include <string.h>
  #endif
  #define __FILE_NAME__         \
    (strrchr(__FILE__,'/')      \
     ? strrchr(__FILE__,'/')+1	\
     : __FILE__                 \
    )
#endif

#define kocl_do_log(level, module, ...) \
    kocl_generic_log(level, module, \
		     __FILE_NAME__, __LINE__, __func__, ##__VA_ARGS__)

#endif
