// 可通过编译、全部测评的 mm.c，实现了隐式链表分配器。
/*
Measuring performance with gettimeofday().

Results for mm malloc:
trace  valid  util     ops      secs  Kops
 0       yes   99%    5694  0.003867  1472
 1       yes   99%    5848  0.003572  1637
 2       yes   99%    6648  0.006157  1080
 3       yes  100%    5380  0.004278  1258
 4       yes   66%   14400  0.000042346154
 5       yes   93%    4800  0.003232  1485
 6       yes   92%    4800  0.002978  1612
 7       yes   55%   12000  0.062193   193
 8       yes   51%   24000  0.210521   114
 9       yes   27%   14401  0.020127   716
10       yes   34%   14401  0.000776 18560
Total          74%  112372  0.317743   354
*/
#define _POSIX_C_SOURCE 200809L  
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "Best",
    /* First member's full name */
    "YeLin",
    /* First member's email address */
    "P29123@126.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};
//@ CSAPP 9.9.12 描述了以下的所有实现原理，可参考。
/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8
/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7) 
#define WSIZE 4
#define DSIZE 8
#define CHUNKSIZE (1 << 12)

#define MAX(x, y) ((x) > (y) ? (x) : (y))
#define PACK(size, alloc) ((size) | (alloc))

/** 
 * @元数据（meta）结构
 * >> head 和 foot 都是 meta。
 * 32位， [_____xxa] 
 * a=0: 本块空闲, a=1: 本块已分配
 * 
 * 每一个分配的块在malloc里称作unit，payload的部分称作data。
 * size:一个块的size为整个块的大小，其数据部分大小要减去两个WSIZE。
 * lab 用的是-m32编译，不会出现大于4GB的分配块，meta用32位足够。
*/
#define GET(p) (*(unsigned int*)(p))
#define PUT(p, val) (*(unsigned int*)(p) = (val))
#define GET_SIZE(p)  (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)

/**
 *@ 块结构
 * |-METASIZE--|                   |----METASIZE--|
 * [ Head      |         Free      |     Foot     ]  
 * ⬆️head_p     ⬆️data_p             ⬆️foot_p
 * meta存储当前整个块的大小，内存上块的分配位，自己的分配位；
 * 块有head和foot两个meta；
 */
#define HDRP(bp) ((char*)(bp) - WSIZE)
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp)  ((char*)(bp) + GET_SIZE(((char*)(bp) - WSIZE)))
#define PREV_BLKP(bp)  ((char*)(bp) - GET_SIZE(((char*)(bp) - DSIZE)))


char* heap_listp;  // 堆起始块（非序言块）指针
// 辅助函数签名
void* extend_heap(size_t words);
void* coalesce(void* bp);
void* find_fit(size_t size);
void place(void* bp, size_t size) ;

/** 
 * mm_init - 初始化 malloc 包；成功返回 0，出错返回 -1。
 * 初始分配一个
*/
int mm_init(void) {
    // mem_sbrk第一次调用返回的指针对齐8字节。
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void*)-1) 
        return -1;
    PUT(heap_listp, 0);  // 填充*
    // 序言块
    PUT(heap_listp + 1 * WSIZE, PACK(DSIZE, 1));   
    PUT(heap_listp + 2 * WSIZE, PACK(DSIZE, 1));
    //* 序言块的末尾为初始指针+12字节，看上去没有对齐8字节。但是，下一个块也有头部，加上
    //* 下一个块的头部后，其数据部分的头就是初始指针+16字节，对齐8字节了。我们要保证数据部分
    //* 长度对齐8字节，其尾块+4字节，下下块头4字节，又让下下块的数据部分对齐8字节。这样所有
    //* 数据块都对齐8字节。
    // 示例： [pad][4head][4foot][head1][对齐8...][foot1][head2][对齐8...][foot2][head3][对齐8...]
    // 尾块
    PUT(heap_listp + 3 * WSIZE, PACK(0, 1));
    heap_listp += 2 * WSIZE;

    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) 
      return -1;
    return 0;
}

/**
 * 功能：释放 bp 指向的块。保证 bp 是之前malloc或realloc调用返回的指针。
 * 返回：无
*/
void mm_free(void *bp) {
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}


/** 
 * 功能：分配数据部分至少有 payload_size 大小的块。
 * 返回：payload指针，必须对齐 8 字节（lab 要求）；错误时返回NULL。
*/
void* mm_malloc(size_t payload_size) {  
    size_t asize;   // adjusted size，实际分配块的大小
    size_t extend_size;
    char* bp;

    if (payload_size == 0) return NULL;
    if (payload_size <= DSIZE) asize = 2 * DSIZE;  // 最小16B，保证payload指针对齐
    else asize = DSIZE * ((payload_size + (DSIZE) + (DSIZE - 1)) / DSIZE); // 向上舍入8倍数
    //@ 以上保证了无论 payload_size 是多少，实际分配块大小 asize 都一定是 8 (DSIZE) 的倍数。

    // 寻找适配的空闲块
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }
    // 没有适配块，增加堆内存。
    extend_size = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extend_size / WSIZE)) == NULL) 
        return NULL;
    place(bp, asize);
    return bp;
}


/**
 * mm_realloc - 重分配块。
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t payload_size;

    // If ptr is NULL, the call is equivalent to malloc(size).
    if (oldptr == NULL)
        return mm_malloc(size);
    
    // If size is zero, the call is equivalent to free(ptr).
    if (size == 0) {
        mm_free(oldptr);
        return NULL;
    }
    newptr = mm_malloc(size);
    if (newptr == NULL)
        return NULL;
      
    payload_size = GET_SIZE(HDRP(oldptr)) - DSIZE;
    // Only copy up to the new size if it's smaller than the old payload size.
    if (size < payload_size)
        payload_size = size;
    memcpy(newptr, oldptr, payload_size);
    mm_free(oldptr);
    return newptr;
}

// 在空闲块中寻找大于size的块，返回其payload指针
void* find_fit(size_t asize) {
    char* p = heap_listp + WSIZE;
    for (; GET_SIZE(p) > 0; p += GET_SIZE(p)) {
        if (!GET_ALLOC(p) && GET_SIZE(p) >= asize) {
            return p + WSIZE;
        }
    }
    return NULL; /* No fit */
}

// 在bp所指的空闲块处，放置 asize 大小的分配块（尝试切割）。
void place(void* bp, size_t asize) {
    size_t have_size = GET_SIZE(HDRP(bp));  // 调用者保证have_size>asize

    // 如果放置了asize，剩余大小不足8，则直接使用；剩余大小大于8，则切割。
    if (have_size - asize < 8) {
        PUT(HDRP(bp), PACK(have_size, 1));
        PUT(FTRP(bp), PACK(have_size, 1));
    }
    else {  // 切割块
        // 为什么剩余空间（空闲块）大小能是8的倍数？
        // have_size（原始空闲块的总大小）从 extend_heap 或 coalesce 得到，因此它是8的倍数。
        // asize（被分配的块大小）也一定是由 mm_malloc 计算出来的，保证是 8 的倍数。
        // 所以，(have_size - asize) 也必然是 8 字节的倍数：
        // 切割后的空闲块的大小也必然是 8 字节的倍数。
        // 但是，当剩余大小恰好为8字节时，会切出8字节大小的无法利用的空闲块。这种空闲块只有和
        //   其他空闲块合并了才有意义。
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(have_size - asize, 0));
        PUT(FTRP(bp), PACK(have_size - asize, 0));
    }
}

/**
 * 功能：尝试合并内存上下空闲块；更新当前块的状态。
 * 返回：合并后块的payload指针。
 * 在每次释放空闲块（free）后立即调用本函数，则不会出现连续多个空闲块。所以，只需要考虑内存上下块。
*/ 
void* coalesce(void* bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    //@ CSAPP Figure 9.40 图示了这四种情况
    if (prev_alloc && next_alloc) return bp;
    else if (prev_alloc && !next_alloc) {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    else if (!prev_alloc && next_alloc) {
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    return bp;

}


/**
 * 功能：扩展堆的大小，创建新快并设置尾块。
 * 返回：新块的payload指针；错误时返回NULL。
 * 该函数会在两种情况下被调用：
 * 1. 初始化堆
 * 2. malloc找不到合适的块。
*/
void* extend_heap(size_t words) {  // size: 新空闲块大小，单位bytes
    char* bp;
    size_t size;
    // 对齐8字节（words单位是4字节，对齐到8）
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    //@ 扩展堆的大小也一定是8的倍数

    if ((long)(bp = mem_sbrk(size)) == -1)  // 扩充size*
        return NULL;

    //& 隐式链表要求每一段内存都必须是某块；如果不是，则无元数据、无法找到下一块。
    //& 所以扩展堆就要新建一个块；而扩展堆会覆盖之前的尾块，所以还需要设置尾块。
    // 设置新块head，foot
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    //* 上面调用mem_sbrk后, bp指向原来的结尾，结尾块转为新块头部
    //* 扩展size后，其前size-4部分就成为数据部分，最后4字节作为新尾块（巧妙的设计）！
    // * [..][end block] -> [..][endb->startb][size - 4(payload)][end block]
    // 设置尾块
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    return coalesce(bp);
}


/** 
 * mm_check - 堆一致性检查器。
 * 空闲链表中的每个内存块是否都被标记为空闲？
 * 是否存在本应合并却未被合并的连续空闲块？
 * 每个空闲块是否确实都包含在空闲链表中？
 * 空闲链表中的指针是否指向有效的空闲块？
 * 是否存在任何已分配内存块重叠的情况？
 * 堆内存块中的指针是否都指向有效的堆地址？
 * 每个块的起始位置是否对齐8字节？
 * ...?
 * ...?
 * 
*/
void mm_check(void) {
   
}