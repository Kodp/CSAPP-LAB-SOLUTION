// log 输出不加尾部标点
// 所有偏移指针运算都要带 (byte*)
// 此实现兼容64位（可去除-m32）
#define _POSIX_C_SOURCE 200809L
#include "mm.h"
#include "memlib.h"
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


// 调试宏
#define DEBUG_LOG 0       // 日志与打印
#define DEBUG_CHECK  0    // 堆检查器，无错误时静默
#define DEBUG_CONTRACT 0  // assert 检查


#if DEBUG_LOG
    #define LOG_IMPLEMENTATION  // // 在项目的一个源文件(.c)中定义实现
    #include "log.h"
    #define log_trace(...) log_log(LOG_TRACE, __FILE__, __LINE__, __VA_ARGS__)
    #define log_debug(...) log_log(LOG_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
    #define log_info(...) log_log(LOG_INFO, __FILE__, __LINE__, __VA_ARGS__)
    #define log_warn(...) log_log(LOG_WARN, __FILE__, __LINE__, __VA_ARGS__)
    #define log_error(...) log_log(LOG_ERROR, __FILE__, __LINE__, __VA_ARGS__)
    #define log_fatal(...) log_log(LOG_FATAL, __FILE__, __LINE__, __VA_ARGS__)
#else
    #define log_trace(...) 
    #define log_debug(...)
    #define log_info(...) 
    #define log_warn(...) 
    #define log_error(...)
    #define log_fatal(...)
#endif

team_t team = {"Steam", "YeLin", "P29123@126.com", "", ""};

#if DEBUG_CONTRACT
    #define ASSERT(COND) assert(COND)
    #define REQUIRES(COND) assert(COND)
    #define ENSURES(COND) assert(COND)
#else
    #define ASSERT(COND) ((void)0)
    #define REQUIRES(COND) ((void)0)
    #define ENSURES(COND) ((void)0)
#endif

#define W_SIZE sizeof(size_t)
#define D_SIZE (W_SIZE + W_SIZE)
#define ALIGNMENT 8  // 作业要求只需对齐8（config.h)
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define HT_SIZE (D_SIZE)  // Head块，Tail块的长度
//@ 占用块无next、prev，+W_SIZE就是数据区
#define EPILOGUE_SIZE W_SIZE  // epilogue 长度
#define PROLOGUE_SIZE W_SIZE  // prologue 长度
#define ALLOC_BLK_FIX_SIZE D_SIZE  // 已分配块的元数据大小：header+footer
#define FREE_BLK_FIX_SIZE (D_SIZE+D_SIZE)  // 空闲块的元数据大小：header+next+prev+footer
#define MAGIC_GAP 2048  // best fit 判定范围：小于这个范围即视为“最佳”
#define MIN_ALK_SIZE (W_SIZE + D_SIZE + W_SIZE) // 最小分配块大小，只能放下一个双字

#define SPLIT_INSERT 0  // 控制split块后，后块头插或尾插

static int size2idx(size_t size);

#define SEGS 12   // 分离链表数量

// #define GET(p) (*(size_t*))(p)
// #define SIZE(p) (GET(P) & 0x3)
// #define ALLOC(p) (GET(p) & 0x1)
typedef unsigned char byte;

// 获取值
static inline size_t read_word(void* p) { 
    REQUIRES(p != NULL);
    return *((size_t*)p); 
}
// *p = val, p为size_t*类型。
// 不可 write_word(next(p)，而是put(next_addr(p))
static inline void write_word(void* p, size_t val) { 
    REQUIRES(p != NULL);
    *((size_t*)p) = val; 
}
static inline size_t get_size(void* p) {
    size_t res = read_word(p) & ~0x3;
    REQUIRES(res != 0);
    return res; 
}
static inline size_t get_alloc(void* p) { 
    return read_word(p) & 0x1; 
}
static inline void get_size_alloc(void*p, size_t* size, size_t* alloc){
    size_t word = read_word(p);
    *size = word & ~0x3;
    *alloc = word & 1;
}

static inline size_t* hdr_addr(void* p) {
    return (size_t*)p; 
}
// 返回next指针的地址
static inline size_t* next_addr(void* p) {
    REQUIRES(p != NULL);      // 要求p!=NULL
    // REQUIRES(get_alloc(p) == 0);  // head和last是“分配块”的同时还要被访问指针
    return (size_t*)((byte*)p + 1 * W_SIZE);
}
// 返回prev指针的地址
static inline size_t* prev_addr(void* p) { 
    REQUIRES(p != NULL);      // 要求p!=NULL
    // REQUIRES(get_alloc(p) == 0);  
    return (size_t*)((byte*)p + 2 * W_SIZE); 
}
static inline size_t* payload_addr(void* p) {
    REQUIRES(p != NULL);
    REQUIRES(get_alloc(p) == 1);  // 要求是分配块
    REQUIRES((size_t)((byte*)p + W_SIZE) % D_SIZE == 0);  // 要求对齐DSIZE
    return (size_t*)((byte*)p + W_SIZE); 
}
static inline size_t* ftr_addr(void* p, size_t size) {  // 要求传入size，防止head没设置
    REQUIRES(p != NULL);
    return (size_t*)((byte*)p + size - W_SIZE);
}

// 接受块头，返回next值
static inline size_t* next(void* p) {
    return (size_t*)(read_word(next_addr(p)));
}
// 接受块头，返回prev值
static inline size_t* prev(void* p) {
    return (size_t*)(read_word(prev_addr(p)));
}

// 接收块头，返回内存下块的地址
static inline size_t* mem_next_addr(void* p) {
    REQUIRES(p != NULL);
    return (size_t*)((byte*)p + get_size(p)); 
}
static inline size_t* s_mem_next_addr(void* p, size_t size) {
    REQUIRES(p != NULL);
    return (size_t*)((byte*)p + size);
}
// 返回内存上块的脚部值
static inline size_t mem_prev_footer(void* p) {
    REQUIRES(p != NULL);
    return *(size_t*)((byte*)p - W_SIZE);  
}
// 接收块头，返回内存上块的地址
static inline size_t* mem_prev_addr(void* p) {
    REQUIRES(p != NULL);
    return (size_t*)((byte*)p - (mem_prev_footer(p) & ~0x3));  // [footer]->footer->p-footer_size
}

// 对齐大小
static inline size_t align(size_t size) {
    switch (size - ALLOC_BLK_FIX_SIZE)  {  // 分配块数据部分大小
        case 112: return ALIGN(128 + ALLOC_BLK_FIX_SIZE);
        case 448: return ALIGN(512 + ALLOC_BLK_FIX_SIZE);
        default: return ALIGN(size); 
    }
}

byte* heads; // 链表头节点数组
byte* tails; // 链表尾节点数组

// head/tail index
#define HTIX(i) (i * HT_SIZE)

void init_log() {
    #if DEBUG_LOG
    log_set_level(LOG_TRACE); // 显示DEBUG及以上级别的日志
    log_set_quiet(1);
    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    // 创建带时间戳的文件名
    char filename[64];
    snprintf(filename, sizeof(filename), "%02d-%02d-%02d_mm.log",
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    FILE* log_file = fopen(filename, "a");
    if (!log_file) {
        fprintf(stderr, "无法打开日志文件\n");
        return;
    }
    log_add_fp(log_file, LOG_TRACE);
    #endif
}

// 设置块为分配块
void mark_alloc(void* p, size_t size) {
    REQUIRES(size != 0);
    size_t mark = size | 1;
    write_word(p, mark);
    write_word(ftr_addr(p, size), mark);
}

// 设置块为空闲块
void mark_free(void* p, size_t size) {
    REQUIRES(size != 0);
    REQUIRES(size % ALIGNMENT == 0);
    // 当size是ALIGNMENT倍数时，低位自然是0
    write_word(p, size);  // size | 0 等于空操作
    write_word(ftr_addr(p, size), size);
}

void init_seglists() {
    log_debug("Execute init_seglists(): Initialize heads and tails");
    size_t half = SEGS * HT_SIZE;    // heads大小
    size_t ht_total_size = D_SIZE + 2 * half; // shift+heads+tails总大小
    byte* heap_st;

    // 分配内存
    if ((heap_st = mem_sbrk(ht_total_size)) == (void*)-1) return;
    // 放置heads，tails
    heads = (byte*)(heap_st);
    tails = (byte*)(heap_st + half);

    // 初始化heads，tails
    /// head tail 比较特殊：它们没有footer和header，仅有next，只占D_SIZE；
    /// 和 next_addr 和 prev_addr 适配，其整体向后移动一个D_SIZE
    for (int i = 0; i < SEGS; ++ i) {
        byte* head = (byte*)heads + HTIX(i);
        byte* tail = (byte*)tails + HTIX(i);
        // 设置head
        write_word(next_addr(head), (size_t)tail);
        write_word(prev_addr(head), (size_t)NULL);

        // 设置tail
        write_word(next_addr(tail), (size_t)NULL);
        write_word(prev_addr(tail), (size_t)head); 
    }
}

// 将p块插入idx编号的空闲链表；不修改元数据。
static inline void insert_head(void* p, int idx) {
    log_trace("Insert %p to seglist[%d] head (head=%p, tail=%p):", 
        p, idx, (byte*)heads + HTIX(idx), (byte*)tails + HTIX(idx));
    byte* head = (byte*)heads + HTIX(idx);
    byte* nxt_head = (byte*)next(head);    // 复用
    write_word(next_addr(p), (size_t)nxt_head);  // p->next = head->next
    write_word(prev_addr(p), (size_t)head);      // p->prev = head
    write_word(prev_addr(nxt_head), (size_t)p);  // head->next->prev=p
    write_word(next_addr(head), (size_t)p);      // head->next = p
}

static inline void insert_tail(void*p, int idx) {
    log_trace("Insert %p to seglist[%d] tail (head=%p, tail=%p):", 
        p, idx, (byte*)heads + HTIX(idx), (byte*)tails + HTIX(idx));
    
    byte* tail = (byte*)tails + HTIX(idx);
    byte* pre_tail = (byte*)prev(tail);
    write_word(next_addr(p), (size_t)tail);
    write_word(prev_addr(p), (size_t)pre_tail);
    write_word(prev_addr(tail), (size_t)p);
    write_word(next_addr(pre_tail), (size_t)p);

}

int mm_init() { 
    init_log();
    log_trace("Execute mm_init()");
    init_seglists();

    // 添加序言块，用于阻挡memprev访问
    log_debug("Add prologue block");
    size_t* prologue;
    if ((prologue = mem_sbrk(PROLOGUE_SIZE)) == (void*)-1)  return -1;
    write_word(prologue, PROLOGUE_SIZE | 1);  // 低3位做size，WSIZE不够3位，提高一点。

    // 分配3个初始块，增加利用率
    log_debug("Add 3 small blocks as initial blocks");
    void* p;
    size_t ifbsize = align(16 + ALLOC_BLK_FIX_SIZE);
    int idx = size2idx(ifbsize);
    for (int j = 0; j < 3; ++ j) {
        if ((p = mem_sbrk(ifbsize)) == (void*)-1) return -1;
        mark_free(p, ifbsize);
        insert_head(p, idx);
    }

    // 添加尾声块，用于阻挡memnext访问
    log_debug("Add epilogue block");
    size_t* epilogue;
    if ((epilogue = mem_sbrk(EPILOGUE_SIZE)) == (void*)-1) return -1;
    write_word(epilogue, EPILOGUE_SIZE | 1);
    return 0;
}

#if DEBUG_CHECK
void check() {
    for (int i = 0; i < SEGS; ++ i) {
        size_t* p = (byte*)heads + HTIX(i);
        size_t* tail = (byte*)tails + HTIX(i);
        int count = 0;
        while(p != tail) {
            size_t* nextp = next_addr(p);
            if (nextp == NULL || (size_t)nextp < 0x3f3f3f3f) {
                log_error("next_addr(p) fail! nextp = %p", nextp);
                phd(100);
                exit(1);
            }
            count ++;
            if (count > 10000) {    
                log_error("Traverse seglist[%d] fail!", i);
                phd(100);
                exit(1);
            }
            p = next(p);
        }
    }
}
#else
#define check()
#endif

size_t* find_best_fit(size_t* head, size_t* tail, size_t asize) {
    size_t* p = next(head);
    size_t* best_fit = NULL;
    size_t best_size = (size_t)-1;
    while (p != tail) {
        // __builtin_prefetch(p + 32/sizeof(size_t), 0, 0);
        // __builtin_prefetch(next(p), 0, 0);
        size_t p_size = get_size(p);
        ssize_t diff = (ssize_t)p_size - (ssize_t)asize;
        if (diff >= 0 && p_size < best_size) {
            best_fit = p, best_size = p_size;
            if (diff < MAGIC_GAP)
                break;
        }
        p = next(p);
    }
    return best_fit;
}

size_t* find_first_fit(size_t* head, size_t* tail, size_t asize) {
    size_t* p = next(head);
    // 四阶循环展开
    while (p != tail) {
        if (get_size(p) >= asize) 
            return p;
        
        size_t* p1 = next(p);
        if (p1 == tail) break;  // 安全边界：防止尾块越界
        if (get_size(p1) >= asize) return p1;
        
        size_t* p2 = next(p1);
        if (p2 == tail) break;  // 安全边界
        if (get_size(p2) >= asize) return p2;
        
        size_t* p3 = next(p2);
        if (p3 == tail) break;  // 安全边界
        if (get_size(p3) >= asize) return p3;

        p = next(p3);
    }
    return NULL;
}

// 从链表中移除空闲块：修改空闲块前一个节点和后一个节点的指针。
static inline void remove_from_free_list(size_t* p) {
    REQUIRES(!get_alloc(p));  // 要求是空闲块
    size_t* p_next = next(p);
    size_t* p_prev = prev(p);
    write_word(next_addr(p_prev), (size_t)p_next);  // p->prev->next=p->next
    write_word(prev_addr(p_next), (size_t)p_prev);  // p->next->prev=p->prev
}

// 功能：尝试分裂一个内存块的后半部分作为新空闲块并插入空闲链表
// 要求：p_size-asize>=MIN_ALK_SIZE; p块asize后的部分为空闲区域
// 前半部分空闲或非空闲都可以，不修改其 alloc 位.
// 分裂后，设置前块大小。后块插入链表。
/**
 * 指针区？不修改
 * 元数据区？修改，仅限于size。
*/
void split_and_insert(size_t* p, size_t front_size) {
    size_t p_alloc, p_size;
    get_size_alloc(p, &p_size, &p_alloc);
    REQUIRES((ssize_t)p_size - (ssize_t)front_size >= MIN_ALK_SIZE);

    log_trace("Execute split_free_block(), front_size=%ld", front_size);
    size_t rear_size = p_size - front_size;
    // 修改前块大小
    write_word(p, front_size | p_alloc); 
    write_word(ftr_addr(p, front_size), front_size | p_alloc);
    // 设置后块头尾并插入链表 
    size_t* rear_blk = (size_t*)((byte*)p + front_size);
    mark_free(rear_blk, rear_size);
    int idx = size2idx(rear_size);
    log_trace("Insert free block with size %ld to seglist[%d]", rear_size, idx);
#if SPLIT_INSERT == 0
    insert_head(rear_blk, idx);
#elif SPLIT_INSERT == 1
    insert_tail(rear_blk, idx);
#endif
    // 确保没有修改分配位
    ENSURES(p_alloc == get_alloc(p));
    // 检查链表是否正确
    check();
}

void* mm_malloc(size_t n) {
    log_trace("Execute mm_malloc(%ld)", n);
    if (n <= 0) return NULL;
    size_t asize = align(n + ALLOC_BLK_FIX_SIZE);  // 分配块大小
    void* p = NULL;
    // 1. 尝试寻找空闲块
    log_trace("Try to find %ld bytes block", asize);
    for (int idx = size2idx(asize); idx < SEGS; ++ idx) {
        byte* head = (byte*)heads + HTIX(idx);
        byte* tail = (byte*)tails + HTIX(idx);
        // 保证第一个遇到的非空链表（无论是否目标链表）都使用 best-fit 策略。
        // 而不是搜索一次没找到，就用first-fit。对比之下，上一种6样本提升1点利用率
        if (next(head) == (size_t*)tail) { // 空链表
            continue;
        }
        // 最后4条链表范围大，用best_fit，对random从94%->95%利用率
        // p = idx < SEGS - 4 ? find_first_fit((size_t*)head, (size_t*)tail, asize) : find_best_fit((size_t*)head, (size_t*)tail, asize);
        
        // 只用first_fit，性能14W->15W(少了p的分支和best_fit)
        p = find_first_fit((size_t*)head, (size_t*)tail, asize);
        if (p != NULL) {  // 找到可用块
            log_debug("Search-times=%d; Find fit block in [%d], size=%d", idx, p, get_size(p));
            remove_from_free_list(p);  // 将找到的空闲块从链表中移除 
            // 如果能分裂，则分裂
            size_t p_size = get_size(p);
            if ((ssize_t)get_size(p) - (ssize_t)asize >= MIN_ALK_SIZE) { 
                split_and_insert(p, asize);
                mark_alloc(p, asize); // 设置为分配块
            }
            else {  // 不改变size
                mark_alloc(p, p_size);
            }
            check();
            return payload_addr(p);  // 返回数据部分
        }
    }

    // 2. 如果堆顶块恰好为空闲块，则扩容此块并使用
    byte* brk = (byte*)(mem_heap_hi()) + 1;
    byte* epilogue = (byte*)brk - EPILOGUE_SIZE;
    size_t last_blk_footer = mem_prev_footer(epilogue);
    if (!(last_blk_footer & 1)) {  // 如果为空闲块
        log_trace("Expand the free block at the top of the heap");
        size_t last_size = last_blk_footer & ~0x3;
        size_t* last_blk = (size_t*)((byte*)epilogue - last_size);
        size_t expand_size = asize - last_size;
        if (mem_sbrk(expand_size) == (void*)-1) return NULL;
        remove_from_free_list(last_blk);  // 从空闲链表中移除
        mark_alloc(last_blk, asize);  // 设置last block大小
        write_word((byte*)last_blk + asize, EPILOGUE_SIZE | 1);  // 设置堆顶尾声块
        return payload_addr(last_blk);
    }

    // 3. 没有可使用的空闲块，添加新块
    log_trace("Extend the top of the heap to add new blocks");
    if ((p = mem_sbrk(asize)) == (void*)-1) return NULL;
    p = (byte*)p - EPILOGUE_SIZE;  // p指向原来尾声块的位置
    mark_alloc(p, asize);  // 设置块
    write_word((byte*)p + asize, EPILOGUE_SIZE | 1); // 设置堆顶尾声块
    return payload_addr(p);
}


/**
 * 功能：尝试将内存上块、本块(p)、内存下块合并为一整块，并尝试从空闲链表中删除内存上块、内存下块。
 * 尝试：如果空闲则使用；非空闲则不使用。
 * 如果前后块存在空闲，且合并后大小能放在前后块里，则返回的是空闲块；
 * 一整块：头部尾部设置为大小、空闲。
 * 对本块的要求：可能会修改本块的头部，但不修改本块数据部分（包含prev，next的区域）。
 * 本块空闲或非空闲均可。
*/
static inline void* coalesce(
    size_t* p, size_t* p_size,   // p_size 作为返回值的一部分，会被修改为新的size。
    size_t* mprev, size_t mprev_size, size_t mprev_alloc, 
    size_t* mnext, size_t mnext_size, size_t mnext_alloc
) {
    log_trace("Execute coalesce(%p)", p);
    size_t* new_block = p;
    size_t new_size = *p_size;
    // 移除前后空闲块
    switch((mprev_alloc << 1) | mnext_alloc) {
        case 0b00:  // 前后都空闲
            new_block = mprev;
            new_size += mprev_size + mnext_size;
            remove_from_free_list(mprev);
            remove_from_free_list(mnext);
            break;
        case 0b01:  // 前空闲，后占用
            new_block = mprev;
            new_size += mprev_size;
            remove_from_free_list(mprev);
            break;
        case 0b10:  // 前占用，后空闲
            new_block = p;
            new_size += mnext_size;
            remove_from_free_list(mnext);
            break;
        case 0b11:  // 前后都占用
            new_block = p;
            break;
    }
    //// 设置合并后的块的大小，并设置为空闲块
    // mark_free(new_block, new_size);
    // 返回合并后的块长度、块头指针
    *p_size = new_size;
    return new_block;
}


void mm_free(void* payload) {
    log_trace("Execute mm_free(%p)", payload);
    // 得到当前块块头信息
    size_t* p = (size_t*)((byte*)payload - W_SIZE);
    size_t p_size = get_size(p);  

    REQUIRES(get_alloc(p) == 1);
    // 得到内存上块信息
    size_t* mprev = mem_prev_addr(p);
    size_t mprev_size, mprev_alloc;
    get_size_alloc(mprev, &mprev_size, &mprev_alloc);
    // 得到内存下块信息
    size_t* mnext = mem_next_addr(p);
    size_t mnext_size, mnext_alloc;
    get_size_alloc(mnext, &mnext_size, &mnext_alloc);
    // 尝试合并前后块
    
    void* new_block = p;
    if (!mnext_alloc || !mprev_alloc) {
        new_block = coalesce(p, &p_size, mprev, mprev_size, mprev_alloc,
            mnext, mnext_size, mnext_alloc);  // 此时p_size被更新为合并后的大小。
        // coalesce不会调用mark_free、设置新块为空闲块
    }
    mark_free(new_block, p_size);  // 标记空闲
    int new_idx = size2idx(p_size);
    //* 在使用find_best_fit（区间best）后，选择性头插或尾插入优化不会继续提高利用率，还会因为多次调用size2idx造成性能损失
    // 如果合并后仍然能属于前块或后块的链表，则说明前块和后块增添了一个更大的块，采用尾插入；否则头插入。
    // int pre_idx = size2idx(mprev_size);
    // int nxt_idx = size2idx(mnext_size);
    // (new_idx == pre_idx || new_idx == nxt_idx) ? insert_tail(new_block, new_idx) : insert_head(new_block, new_idx);
    insert_tail(new_block, new_idx);  // 合并后的块增大且可能不改变目标链表，尾插让大块放后面，减少外部碎片。

}


#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

void* mm_realloc(void* payload, size_t n) {
    if (n == 0) {
        mm_free(payload);
        return NULL;
    }
    else if (payload == NULL) {
        return mm_malloc(n);
    }
    size_t* p = (size_t*)((byte*)payload - W_SIZE);
    size_t p_size = get_size(p);   // 当前块大小
    size_t asize = align(n + ALLOC_BLK_FIX_SIZE);  // 待分配大小
    // 1. 待分配大小小于等于当前块大小（不会触发）
    // if (asize <= p_size) { 
        // return payload;
        // 待分配大小足够小，可以切分
        // if ((ssize_t)p_size - (ssize_t)asize >= MIN_ALK_SIZE) {
        //     return NULL;
        // }
    // }

    // 2. 如果是堆顶块，则扩容
    byte* brk = (byte*)(mem_heap_hi()) + 1;
    byte* epilogue = (byte*)brk - EPILOGUE_SIZE;
    if ((byte*)p + p_size == epilogue) {  // 堆顶块
        size_t expand_size = asize - p_size;
        if (mem_sbrk(expand_size) == (void*)-1) return NULL;
        mark_alloc(p, asize);
        write_word((byte*)epilogue + expand_size, EPILOGUE_SIZE | 1);
        return payload;  // 指针不动
    }
    // 3. 计算合并前后空闲块的最大大小，如果足够则合并、使用
    // 得到内存上块信息
    size_t payload_size = p_size - ALLOC_BLK_FIX_SIZE;
    // // 没有出现过合并后足够大（下面代码可通过测评）
    size_t* mprev = mem_prev_addr(p);
    size_t mprev_size, mprev_alloc;
    get_size_alloc(mprev, &mprev_size, &mprev_alloc);
    // 得到内存下块信息
    size_t* mnext = mem_next_addr(p);
    size_t mnext_size, mnext_alloc;
    get_size_alloc(mnext, &mnext_size, &mnext_alloc);
    size_t new_size = p_size + (mprev_alloc ? 0 : mprev_size) + (mnext_alloc ? 0 : mnext_size);
    ssize_t diff = (ssize_t)new_size - (ssize_t)asize;
    if (diff >= 0) {  // 合并后足够大，合并
        void* new_block = coalesce(
            p, &p_size, mprev, mprev_size, mprev_alloc, mnext, mnext_size, mnext_alloc);
        memmove(payload, payload_addr(new_block), payload_size);
        if (diff >= MIN_ALK_SIZE) { 
            split_and_insert(new_block, asize);
        }
        return payload_addr(new_block);
    }

    // 4. 分配新块
    void* new_payload = mm_malloc(n);
    if (new_payload == NULL) return NULL;
    memcpy(new_payload, payload, payload_size);
    mm_free(payload);
    return new_payload;
}

static inline int size2idx(size_t size) {
    REQUIRES(size % 8 == 0);  // 对齐后的size
    if (LIKELY(size < 4080)) {
        if (LIKELY(size < 136)) {
            if (LIKELY(size <= 112)) {
                if (LIKELY(size <= 24)) { // [16, 24] (min=16), freq=9337
                    return 0;
                }
                else { // [32, 112], freq=3198
                    return 1;
                }
            }
            else { // size>112, 从120开始：[120, 128], freq=4004
                return 2; 
            }
        }
        else if (size == 136) {  // freq=8857
            return 3;
        }
        else { // [144, 4072]
            if (size <= 456) { // [144, 456], freq=3084
                return 4;
            }
            else {  // [464,4072], freq=2767
                return 5;
            }
        }
    }
    else if (size == 4080) {  // freq=8697
        return 6;
    }
    else { // [4088, inf]
        if (size <= 95112) {
            if (size <= 8552) {
                if (size <= 4104) {  // [4088(4096), 4104], freq=4806
                    return 7;
                }
                else { // [4112, 8552], freq=4057
                    return 8;
                }
            }
            else if (size <= 20032) {  // [8560, 20032], freq=4058 
                return 9;
            }
            else {  // [20040, 95112]
                return 10;
            }
        }
        else {  // [95520, inf]
            return 11;
        }
    }
}


void phd(int limit) {  // print_heap_debug
    puts("────────────────────────────────── HEAP ──────────────────────────────────");
    printf("Heads:\n");
    for (int i = 0; i < SEGS; ++ i) {
        byte* head = (byte*)heads + HTIX(i);
        printf("%p:[%8zx,%8zx] ", head, (size_t)((byte*)head + W_SIZE), (size_t)((byte*)head + 2 * W_SIZE));
        if (i % 4 == 3) puts("");
    }
    printf("\nTails:\n");
    for (int i = 0; i < SEGS; ++ i) {
        byte* tail = (byte*)tails + HTIX(i);
        printf("%p:[%8zx,%8zx] ", tail, (size_t)((byte*)tail + W_SIZE), (size_t)((byte*)tail + 2 * W_SIZE));
        if (i % 4 == 3) puts("");
    }
    
    printf("\nPrologue:\n");
    byte* pt = (byte*)heads + 2 * SEGS * HT_SIZE;
    printf("[%zx]", read_word(pt));
    pt += PROLOGUE_SIZE;

    printf("\n\nAfter:\n");
    
    byte* brk = (byte*)(mem_heap_hi()) + 1;
    for (int i = 0; i < limit; ++ i) {
        size_t cur_size = read_word(pt) & ~0x3;
        size_t cur_alloc = read_word(pt) & 1;
        if (cur_size == 0) {
            log_warn("[%8p] cur_size = 0, break", pt);
            break;
        }
        if (cur_alloc) {
            printf("%10p:[%8zx %8s %8s %8zx] ", 
                pt, read_word(pt), "alloc", "alloc" , read_word(ftr_addr(pt, cur_size)));
        }
        else {
            printf("%10p:[%8zx %8zx %8zx %8zx] ", 
                pt, read_word(pt), (size_t)next(pt), (size_t)prev(pt), read_word(ftr_addr(pt, cur_size))); 
        }
        
        pt += cur_size;
        if (pt >= (byte*)brk - EPILOGUE_SIZE) {
            printf("\n pt=%p(%p) >= epilogue=%p(%p), break.\n", 
                (void*)pt, (void*)read_word(pt), (byte*)(brk - EPILOGUE_SIZE), (void*)read_word((byte*)brk - EPILOGUE_SIZE));
            break;
        }
        if (i % 2 == 1) puts("");
    }

}



