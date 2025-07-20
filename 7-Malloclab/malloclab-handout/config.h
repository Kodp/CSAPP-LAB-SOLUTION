#ifndef __CONFIG_H_
#define __CONFIG_H_

/*
 * config.h - malloc lab configuration file
 *
 * Copyright (c) 2002, R. Bryant and D. O'Hallaron, All rights reserved.
 * May not be used, modified, or copied without permission.
 */

#define USE_CSAPP 1 /* Set to 1 to use CS:APP traces, 0 for PKU traces */


// 衡量时间时，执行的重复次数。
// 可视化或调试时设置为1
#define REPEAT 10

/*
 * This is the default path where the driver will look for the
 * default tracefiles. You can override it at runtime with the -t flag.
 */
#if USE_CSAPP
  #define TRACEDIR "traces/"
#else
  #define TRACEDIR "pku_traces/"  // pku malloc lab 占用率达到90即为占用率满分。
#endif
/*
 * This is the list of default tracefiles in TRACEDIR that the driver
 * will use for testing. Modify this if you want to add or delete
 * traces from the driver's test suite. For example, if you don't want
 * your students to implement realloc, you can delete the last two
 * traces.
 */
#if USE_CSAPP
  #define DEFAULT_TRACEFILES \
    "amptjp-bal.rep",\
    "cccp-bal.rep",\
    "cp-decl-bal.rep",\
    "expr-bal.rep",\
    "coalescing-bal.rep",\
    "random-bal.rep",\
    "random2-bal.rep",\
    "binary-bal.rep",\
    "binary2-bal.rep",\
    "realloc-bal.rep",\
    "realloc2-bal.rep"
  
#else  
// pku_traces
  #define DEFAULT_TRACEFILES \
      "alaska.rep", \
      "amptjp.rep", \
      "bash.rep", \
      "boat.rep",\
      "boat-plus.rep", \
      "binary2-bal.rep",\
      "cccp.rep", \
      "cccp-bal.rep", \
      "chrome.rep", \
      "coalesce-big.rep",  \
      "coalescing-bal.rep", \
      "corners.rep", \
      "cp-decl.rep", \
      "exhaust.rep", \
      "expr-bal.rep", \
      "firefox-reddit2.rep", \
      "freeciv.rep", \
      "malloc.rep", \
      "malloc-free.rep", \
      "perl.rep", \
      "random.rep", \
      "random2.rep", \
      "realloc.rep"
#endif
// 没有在官方config.h里，但是在trace里的两个额外测例
  // "cp-decl-bal.rep", \
  // "expr-bal.rep"

  // "random-bal.rep",\
  // "random2-bal.rep"

/*
 * This constant gives the estimated performance of the libc malloc
 * package using our traces on some reference system, typically the
 * same kind of system the students use. Its purpose is to cap the
 * contribution of throughput to the performance index. Once the
 * students surpass the AVG_LIBC_THRUPUT, they get no further benefit
 * to their score.  This deters students from building extremely fast,
 * but extremely stupid malloc packages.
 */
// #define AVG_LIBC_THRUPUT      90000E3  /* 900000 Kops/sec, 基于 i9-13950HX和上次的结果 */
#define AVG_LIBC_THRUPUT      10000E3  /* 900000 Kops/sec, 基于 i9-13950HX和上次的结果 */

 /* 
  * This constant determines the contributions of space utilization
  * (UTIL_WEIGHT) and throughput (1 - UTIL_WEIGHT) to the performance
  * index.  
  */
#define UTIL_WEIGHT .60

/* 
 * Alignment requirement in bytes (either 4 or 8) 
 */
#define ALIGNMENT 8  

/* 
 * Maximum heap size in bytes 
 */
#if USE_CSAPP
  #define MAX_HEAP (20*(1<<20))  /* 20 MB */
#else
  #define MAX_HEAP (100*(1<<20))  /* 100 MB, mainly due to corner.rep */
#endif
/*****************************************************************************
 * Set exactly one of these USE_xxx constants to "1" to select a timing method
 *****************************************************************************/
// 使用gettod，因为fcyc在我的wsl上数据波动非常大；可能是由于wsl和硬件计数器适配不够好导致的。
// 用fcyc的性能更好看。
#define USE_FCYC   0   /* cycle counter w/K-best scheme (x86 & Alpha only) */
#define USE_ITIMER 0   /* interval timer (any Unix box) */
#define USE_GETTOD 1   /* gettimeofday (any Unix box) */

#endif /* __CONFIG_H */
