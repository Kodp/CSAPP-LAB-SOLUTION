/**
 * 块结构：
 * 
 * 分配块:
 * +--------+--------------------------------+--------+
 * | HEADER |       PAYLOAD (aligned)        | HEADER |
 * +--------+--------------------------------+--------+
 *   size_t ^         8 align                  size_t
 *          |
 *          payload pointer
 * 
 * 空闲块:
 * +--------+--------------------------------+
 * | HEADER |     POINTERS    | ... | FOOTER |
 * +--------+--------------------------------+
 *   size_t ^                          size_t
 *          |
 *          payload pointer
 *  
 * HEADER:
 * +----------------+---+---+---+
 * |      SIZE      |   |   | A |
 * +----------------+---+---+---+
 *                           1b
 * A: 当前块是否分配
 * FOOTER:
 * +----------------+---+---+---+
 * |      SIZE      |   |   | A |
 * +----------------+---+---+---+
 *                           1b
 * A: 当前块是否分配
 * 
 * 
 * 注意事项：
 * 
 * log 输出不加尾部标点
 * 所有偏移指针运算都要带 (byte*)
 * 此实现兼容64位（可去除-m32）
*/

#define _POSIX_C_SOURCE 200809L  // for vscode IntelliSense
#include "mm.h"
#include "memlib.h"
#include <stddef.h>
#include <assert.h>
#include <string.h>
team_t team = {"Steam", "syh", "P29123@126.com", "", ""};

// 调试宏
#define DEBUG_LOG 0       // 日志与打印
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
    int log_initialized = 0;
#else
    #define log_trace(...) ((void)0)
    #define log_debug(...) ((void)0)
    #define log_info(...)  ((void)0)
    #define log_warn(...)  ((void)0)
    #define log_error(...) ((void)0)
    #define log_fatal(...) ((void)0)
#endif
#if DEBUG_CONTRACT
    #define ASSERT(COND) assert(COND)
    #define REQUIRES(COND) assert(COND)
    #define ENSURES(COND) assert(COND)
#else
    #define ASSERT(COND) ((void)0)
    #define REQUIRES(COND) ((void)0)
    #define ENSURES(COND) ((void)0)
#endif


#define W_SIZE (sizeof(size_t))
#define D_SIZE (W_SIZE + W_SIZE)
#define ALIGNMENT 8  // 作业要求只需对齐8（config.h)
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define META_BLK_SIZE (D_SIZE)  // Head块，Tail块的长度
#define EPILOGUE_SIZE (W_SIZE)  // epilogue 长度
#define PROLOGUE_SIZE (W_SIZE)  // prologue 长度
#define ALLOC_BLK_FIX_SIZE (D_SIZE)  // 已分配块的元数据大小：header+footer
#define FREE_BLK_FIX_SIZE (D_SIZE+D_SIZE)  // 空闲块的元数据大小：header+next+prev+footer
#define MAGIC_GAP 112  // best fit 判定范围：小于这个范围即视为“最佳”
#define MIN_ALK_SIZE (W_SIZE + D_SIZE + W_SIZE) // 最小链表块大小，只能放下一个双字
#define MIN_SPLIT_SIZE (4 * D_SIZE)  // 至少要大于最小树块；树的指针多些，最小块大小大于最小链表块大小。

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define SEGS 8   // 分离链表数量

#define HTIX(i) (i * META_BLK_SIZE) // seg_heads，seg_tails

#define SEG_HEAD(i) ((byte*)seg_heads + (i) * META_BLK_SIZE) 
#define SEG_TAIL(i) ((byte*)seg_tails + (i) * META_BLK_SIZE)

typedef unsigned char byte;


byte* seg_heads; // 链表头节点数组
byte* seg_tails; // 链表尾节点数组

static int size2idx(size_t size);

static inline size_t read_word(void* p) { 
    REQUIRES(p != NULL); return *((size_t*)p); 
}
static inline void write_word(void* p, size_t val) { 
    REQUIRES(p != NULL); *((size_t*)p) = val; 
}
static inline size_t get_size(void* p) {
    size_t res = read_word(p) & ~3;  // 最小能够读出的大小为4，适合尾声块
    REQUIRES(res != 0);
    return res; 
}
static inline int get_alloc(void* p) { return read_word(p) & 1; }
static inline void get_size_alloc(void*p, size_t* size, size_t* alloc){
    size_t word = read_word(p);
    *size = word & ~0x3;
    *alloc = word & 1;
}
static inline void get_size_pa(void* p, size_t* size, size_t* pa) {
    size_t word = read_word(p);
    *size = word & ~0x3;
    *pa = word & 3;
}
// 返回next指针的地址
static inline size_t* next_addr(void* p) {
    REQUIRES(p != NULL);      // 要求p!=NULL
    // REQUIRES(get_alloc(p) == 0);  // head和last是“分配块”的同时还要被访问指针
    return (size_t*)((byte*)p + 1 * W_SIZE);
}
// 返回prev指针的地址
static inline size_t* prev_addr(void* p) { 
    REQUIRES(p != NULL);
    return (size_t*)((byte*)p + 2 * W_SIZE); 
}
// 返回数据部分地址
static inline size_t* payload_addr(void* p) {
    REQUIRES(p != NULL);
    REQUIRES(get_alloc(p) == 1);  // 要求是分配块
    REQUIRES((size_t)((byte*)p + W_SIZE) % 8 == 0);  // 要求对齐8
    return (size_t*)((byte*)p + W_SIZE); 
}
// 返回footer地址
static inline size_t* ftr_addr(void* p, size_t size) {  // 要求传入size，防止head没设置
    REQUIRES(p != NULL);
    return (size_t*)((byte*)p + size - W_SIZE);
}

// 返回next值
static inline size_t* next(void* p) {
    return (size_t*)(read_word(next_addr(p)));
}
// 返回prev值
static inline size_t* prev(void* p) {
    return (size_t*)(read_word(prev_addr(p)));
}
// 返回内存下块的地址
static inline size_t* mem_next_addr(void* p, size_t size) {
    REQUIRES(p != NULL);
    return (size_t*)((byte*)p + size);
}
// 返回内存上块的脚部值
static inline size_t mem_prev_footer(void* p) {
    REQUIRES(p != NULL);
    return *(size_t*)((byte*)p - W_SIZE);  
}
// 返回内存上块的地址
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


void init_log() {
#if DEBUG_LOG
if (log_initialized == 0) { // 防止重复调用 mm_init 导致回调函数重复注册。
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
    log_initialized = 1;
    printf("Log file name: %s\n", filename);
}
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
    write_word(p, size);  // 当size对齐ALIGNMENT时，size | 0 等于空操作
    write_word(ftr_addr(p, size), size);
}

// 将p块插入idx编号的空闲链表；不修改元数据。
static inline void insert_head(void* p, int i) {
    log_trace("Insert %p to seglist[%d] head (head=%p, tail=%p):", 
        p, i, SEG_HEAD(i), SEG_TAIL(i));
    
    byte* head = SEG_HEAD(i);
    byte* nxt_head = (byte*)next(head);
    write_word(next_addr(p), (size_t)nxt_head);  // p->next = head->next
    write_word(prev_addr(p), (size_t)head);      // p->prev = head
    write_word(prev_addr(nxt_head), (size_t)p);  // head->next->prev=p
    write_word(next_addr(head), (size_t)p);      // head->next = p
}

static inline void insert_tail(void* p, int i) {
    log_trace("Insert %p to seglist[%d] tail (head=%p, tail=%p):", 
        p, i, SEG_HEAD(i), SEG_TAIL(i));
    
    byte* tail = SEG_TAIL(i);
    byte* pre_tail = (byte*)prev(tail);
    write_word(next_addr(p), (size_t)tail);     // p->next = tail
    write_word(prev_addr(p), (size_t)pre_tail); // p->prev = tail->prev
    write_word(prev_addr(tail), (size_t)p);     // tail->prev = p
    write_word(next_addr(pre_tail), (size_t)p); // tail->prev->next = p
}

void init_data_structure() {
    log_debug("Execute init_data_structure(): Initialize seg_heads and seg_tails");
    size_t half = SEGS * META_BLK_SIZE;    // seg_heads大小
    size_t ht_total_size = D_SIZE + 2 * half; // shift+seg_heads+seg_tails总大小
    byte* heap_st;

    // 分配内存
    if ((heap_st = mem_sbrk(ht_total_size)) == (void*)-1) return;
    // 放置seg_heads，seg_tails
    seg_heads = (byte*)(heap_st);
    seg_tails = (byte*)(heap_st + half);
    

    // 初始化seg_heads，seg_tails
    /// head tail 比较特殊：它们没有footer和header，仅有next，只占D_SIZE；
    /// 和 next_addr 和 prev_addr 适配，其整体向后移动一个D_SIZE
    int i = 0;
    for (; i < SEGS; ++ i) {
        byte* head = SEG_HEAD(i);
        byte* tail = SEG_TAIL(i);
        // 设置head
        write_word(next_addr(head), (size_t)tail);
        write_word(prev_addr(head), (size_t)NULL);
        // write_word(prev_addr(head), insert_tail)

        // 设置tail
        write_word(next_addr(tail), (size_t)NULL);
        write_word(prev_addr(tail), (size_t)head); 
    }
    
}

int mm_init() { 
    init_log();
    log_trace("Execute mm_init()");
    init_data_structure();

    // 添加序言块，用于阻挡memprev访问
    log_debug("Add prologue block");
    size_t* prologue;
    if ((prologue = mem_sbrk(PROLOGUE_SIZE)) == (void*)-1)  return -1;
    write_word(prologue, PROLOGUE_SIZE | 1);  // 低3位做size，WSIZE不够3位，提高一点。

    // 分配3个初始块，增加利用率
    log_debug("Add 3 small blocks as initial blocks");
    void* p;
    size_t ifbsize = align(16 + ALLOC_BLK_FIX_SIZE);
    int i = size2idx(ifbsize);
    for (int j = 0; j < 3; ++ j) {
        if ((p = mem_sbrk(ifbsize)) == (void*)-1) return -1;
        mark_free(p, ifbsize);
        insert_tail(p, i);
    }

    // 添加尾声块，用于阻挡memnext访问
    log_debug("Add epilogue block");
    size_t* epilogue;
    if ((epilogue = mem_sbrk(EPILOGUE_SIZE)) == (void*)-1) return -1;
    write_word(epilogue, EPILOGUE_SIZE | 1);
    return 0;
}

static inline size_t* find_best_fit(int i, size_t a_size) {
    byte* head = SEG_HEAD(i), *tail = SEG_TAIL(i);
    size_t* p = next(head);
    size_t* best_fit = NULL;
    size_t best_size = (size_t)-1;
    while (p != tail) {
        // __builtin_prefetch(p + 32/sizeof(size_t), 0, 0);
        // __builtin_prefetch(next(p), 0, 0);
        size_t p_size = get_size(p);
        ssize_t diff = (ssize_t)p_size - (ssize_t)a_size;
        if (diff >= 0 && p_size < best_size) {
            best_fit = p, best_size = p_size;
            if (diff < MAGIC_GAP)
                break;
        }
        p = next(p);
    }
    return best_fit;
}

static inline size_t* find_first_fit(int i,  size_t a_size) {
    byte *head = SEG_HEAD(i), *tail = SEG_TAIL(i);
    size_t* p = next(head), *p1;
    if (p == tail) {
        log_trace("No free block in seglist[%d]", i);
        return NULL;  // 没有空闲块
    }

    while (p != tail) {
        // 用read_word替代get_size效果一样，因为这些都是空闲块，header=size
        // 即使header!=size，仍然有 size<=header<size+8,当大于a_size的时候，则空间足够且恰好
        if (read_word(p) >= a_size)  return p;
        p1 = next(p);
        if (p1 == tail) break;  // 安全边界：防止尾块越界

        if (read_word(p1) >= a_size) return p1;
        p = next(p1);
    }
    return NULL;
}

// 从链表中移除空闲块：修改空闲块前一个节点和后一个节点的指针。
static inline void seg_remove(void* p, size_t p_size) {
    REQUIRES(!get_alloc(p));  // 要求是空闲块
    size_t* p_next = next(p);
    size_t* p_prev = prev(p);
    write_word(next_addr(p_prev), (size_t)p_next);  // p->prev->next=p->next
    write_word(prev_addr(p_next), (size_t)p_prev);  // p->next->prev=p->prev
}

// 功能：尝试分裂一个内存块的后半部分作为新空闲块并插入空闲链表
// 要求：p_size-a_size>=MIN_SPLIT_SIZE; p块a_size后的部分为空闲区域
// 前半部分空闲或非空闲都可以，不修改其 alloc 位.
// 分裂后，设置前块大小。后块插入链表。
void split_and_insert(void* p, size_t p_size, size_t front_size, int front_alloc) {
    REQUIRES((ssize_t)p_size - (ssize_t)front_size >= MIN_SPLIT_SIZE);

    size_t rear_size = p_size - front_size;
    size_t* rear_blk = (size_t*)((byte*)p + front_size);
    log_trace("Execute split_and_insert(), front_size=%ld, rear_size=%ld", front_size, rear_size);

    // 设置前块元数据
    write_word(p, front_size | front_alloc); 
    write_word((byte*)rear_blk - W_SIZE, front_size | front_alloc);

    // 设置后块元数据并插入链表 
    mark_free(rear_blk, rear_size);
    log_trace("Insert free block with size %ld to seglist[%d]", rear_size, size2idx(rear_size););
    insert_tail(rear_blk, size2idx(rear_size));  // 插入到链表尾部
}


void* mm_malloc(size_t n) {
    log_trace("Execute mm_malloc(%ld)", n);
    if (n <= 0) return NULL;

    size_t a_size, p_size, top_size, expand_size;
    size_t *top_blk, top_blk_footer;
    byte *head, *tail, *brk, *epilogue;
    void *p, *new_blk;

    // 0. 计算分配块大小
    a_size = align(n + ALLOC_BLK_FIX_SIZE);  
    a_size = MAX(a_size, MIN_ALK_SIZE);

    // 1. 扫描空闲块
    log_trace("Try to find %ld bytes block", a_size);
    int i;
    for (i = size2idx(a_size); i < SEGS; ++ i) {
        p = find_first_fit(i, a_size);

        // 找到可用块
        if (p != NULL) {
            log_debug("i=%d; Find fit block in [%p], size=%zd", i, p, get_size(p));
            p_size = get_size(p);
            seg_remove(p, p_size);  // 将找到的空闲块从链表中移除 
            
            // 足够大则分裂块
            if ((ssize_t)p_size - (ssize_t)a_size >= MIN_SPLIT_SIZE)   
                // split_and_insert 会设为前块的分配位。
                split_and_insert(p, p_size, a_size, 1); 
            else 
                // 仅设为分配块，不改变size
                mark_alloc(p, p_size); 
            return payload_addr(p);  // 返回数据部分    
            
        }
    }

    // 3. 使用堆顶空闲块（若存在）
    // 如果堆顶存在空闲块，则从空闲块开始；否则从堆顶开始
    brk = (byte*)(mem_heap_hi()) + 1;
    epilogue = (byte*)brk - EPILOGUE_SIZE;
    top_blk_footer = mem_prev_footer(epilogue);

    if (!(top_blk_footer & 1)) {  
        log_trace("Expand the free block at the top of the heap");
        top_size = top_blk_footer & ~0x3;
        top_blk = (size_t*)((byte*)epilogue - top_size);
        expand_size = a_size - top_size;
        if (mem_sbrk(expand_size) == (void*)-1) return NULL;
        seg_remove(top_blk, top_size);  // 从链表中移除空闲块
        mark_alloc(top_blk, a_size);  // 设置last block大小
        write_word((byte*)top_blk + a_size, EPILOGUE_SIZE | 1);  // 设置堆顶尾声块
        return payload_addr(top_blk);
    }

    // 4. 添加新块
    log_trace("Extend the top of the heap to add new blocks");
    if ((new_blk = mem_sbrk(a_size)) == (void*)-1) return NULL;
    new_blk = (byte*)new_blk - EPILOGUE_SIZE;  // p指向原来尾声块的位置
    mark_alloc(new_blk, a_size);  // 设置块
    write_word((byte*)new_blk + a_size, EPILOGUE_SIZE | 1); // 设置堆顶尾声块
    return payload_addr(new_blk);
}

static inline void coalesce(void* p, void** new_p, size_t* new_p_size) {
    log_trace("Execute coalesce(%p)", p);
    REQUIRES(get_alloc(p) == 1);

    // 获取邻居信息
    size_t p_size = get_size(p), new_size = p_size;  
    size_t* mprev_foot = (size_t*)((byte*)p - W_SIZE);
    size_t* mnext = mem_next_addr(p, p_size);
    size_t mprev_size, mprev_alloc, mnext_size, mnext_alloc;
    get_size_alloc(mprev_foot, &mprev_size, &mprev_alloc);
    get_size_alloc(mnext, &mnext_size, &mnext_alloc);
    size_t* mprev = (size_t*)((byte*)p - mprev_size);

    // 合并前，先从链表/树中移除空闲的邻居;计算新块的起始地址和大小
    if (!mprev_alloc) {
        seg_remove(mprev, mprev_size);
        new_size += mprev_size;
        p = mprev;  // 更新p为新块
    }
    if (!mnext_alloc) {
        seg_remove(mnext, mnext_size);
        new_size += mnext_size;
    }

    // 设置新块的元数据
    mark_free(p, new_size);  // 标记空闲
    *new_p = p;
    *new_p_size = new_size;
}

void mm_free(void* payload) {
    log_trace("Execute mm_free(%p)", payload);
    if (payload == NULL) return;
    size_t* p = (size_t*)((byte*)payload - W_SIZE);
    REQUIRES(get_alloc(p) == 1);

    void* new_blk;
    size_t new_size;
    coalesce(p, &new_blk, &new_size);
    
    //* 在使用find_best_fit（区间best）后，选择性头插或尾插入优化不会继续提高利用率，还会因为多次调用size2idx造成性能损失

    insert_tail(new_blk, size2idx(new_size));  // 插入到链表尾部
}



#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

void* mm_realloc(void* payload, size_t n) {
    log_trace("Execute mm_realloc(payload=%p, n=%zd)", payload, n);
    if (n == 0) {
        mm_free(payload);
        return NULL;
    }
    else if (payload == NULL) {
        return mm_malloc(n);
    }

    size_t* p = (size_t*)((byte*)payload - W_SIZE);
    size_t p_size = get_size(p);   // 当前块大小
    size_t a_size = align(n + ALLOC_BLK_FIX_SIZE);  // 待分配大小
    a_size = MAX(a_size, MIN_ALK_SIZE);
    // 1. 待分配大小小于等于当前块大小
    if (a_size <= p_size) {
        return payload;
        // 待分配大小足够小，可以切分
        // if ((ssize_t)p_size - (ssize_t)a_size >= MIN_SPLIT_SIZE) {
        // }
    }
    
    // 2. 如果是堆顶块，则扩容
    byte* brk = (byte*)(mem_heap_hi()) + 1;
    byte* epilogue = (byte*)brk - EPILOGUE_SIZE;
    if ((byte*)p + p_size == epilogue) {  // 堆顶块
        size_t expand_size = a_size - p_size;
        if (mem_sbrk(expand_size) == (void*)-1) return NULL;
        mark_alloc(p, a_size);
        write_word((byte*)epilogue + expand_size, EPILOGUE_SIZE | 1);
        return payload;  // 指针不动
    }

    //  3. 尝试合并前后空闲块使用; 合并足够大则使用；不够大，则释放合并块，采用新块。
    size_t payload_size = p_size - ALLOC_BLK_FIX_SIZE;
    size_t new_size = p_size;
    byte* new_blk;
    coalesce(p, &new_blk, &new_size);  
    ssize_t diff = (ssize_t)new_size - (ssize_t)a_size;

    // 如果合并后足够用，则合并(不够用则必然需要释放)
    if (diff >= 0) {  
        mark_alloc(new_blk, new_size);  // 设置新块为分配块

        // 拷贝数据
        memmove(payload_addr(new_blk), payload, payload_size);
        
        // 如果大小足够分裂，则分裂
        if (diff >= MIN_SPLIT_SIZE) 
            split_and_insert(new_blk, new_size, a_size, 1);
        
        return payload_addr(new_blk);
    }
    else {
        // 4. 分配新块
        mark_alloc(new_blk, new_size);  // 设置新块为分配块，防止合并了堆顶块变成堆顶块使用
        void* new_payload = mm_malloc(n);
        if (new_payload == NULL) return NULL;
        memcpy(new_payload, payload, payload_size);

        // 设置新块为未分配块，并插入
        mark_free(new_blk, new_size);  
        insert_tail(new_blk, size2idx(new_size));  // 插入到链表尾部
        
        return new_payload;
    }
}


#define SIZE2IDX_V4
static inline int size2idx(size_t size) {
    REQUIRES(size % 8 == 0); // 对齐后的size
    if (LIKELY(size <= 4080)) {
        if (LIKELY(size < 136)) {
            if (LIKELY(size <= 128)) {
                if (LIKELY(size <= 24)) {
                    return 0;
                } else if (size <= 112) {
                    return 1;
                } else {
                    return 2;
                }
            }
        } else if (size == 136) {
            return 3;
        } else if (size <= 4072) {
            if (size <= 456) {
                return 4;
            } else {
                return 5;
            }
        } else {
            return 6;
        }
    } else {
        return 7;
    }
    return -1; // Nop
}

void mm_checkheap(int lineno) {
    // 框架会执行该函数；置空。
}



void phd(int limit) {  // print_heap_debug
    puts("────────────────────────────────── HEAP ──────────────────────────────────");
    printf("Heads:\n");
    for (int i = 0; i < SEGS; ++ i) {
        byte* head = SEG_HEAD(i);
        printf("%p:[%8zx,%8zx] ", head, (size_t)((byte*)head + W_SIZE), (size_t)((byte*)head + 2 * W_SIZE));
        // if (i % 4 == 3) 
            puts("");
    }
    printf("\nTails:\n");
    for (int i = 0; i < SEGS; ++ i) {
        byte* tail = SEG_TAIL(i);
        printf("%p:[%8zx,%8zx] ", tail, (size_t)((byte*)tail + W_SIZE), (size_t)((byte*)tail + 2 * W_SIZE));
        // if (i % 4 == 3) 
            puts("");
    }
    
    printf("\nPrologue:\n");
    byte* pt = (byte*)seg_heads + 2 * SEGS * META_BLK_SIZE + D_SIZE;
    printf("[%zx]", read_word(pt));
    pt += PROLOGUE_SIZE;

    printf("\n\nAfter:\n");
    
    byte* brk = (byte*)(mem_heap_hi()) + 1;
    for (int i = 0; i < limit; ++ i) {
        size_t cur_size = read_word(pt) & ~0x3;
        size_t cur_alloc = read_word(pt) & 1;
        if (cur_size == 0) {
            printf("[%12p] cur_size = 0, break", pt);
            break;
        }
        if (cur_alloc) {
            printf("%p:[%8zx %12s %12s %8zx] ", 
                pt, read_word(pt), "alloc", "alloc" , read_word(ftr_addr(pt, cur_size)));
        }
        else {
            printf("%p:[%8zx %12zx %12zx %8zx] ", 
                pt, read_word(pt), (size_t)next(pt), (size_t)prev(pt), read_word(ftr_addr(pt, cur_size))); 
        }
        
        pt += cur_size;
        if (pt >= (byte*)brk - EPILOGUE_SIZE) {
            printf("\n pt=%p(%p) >= epilogue=%p(%p), break.\n", 
                (void*)pt, (void*)read_word(pt), (byte*)(brk - EPILOGUE_SIZE), (void*)read_word((byte*)brk - EPILOGUE_SIZE));
            break;
        }
        // if (i % 2 == 1) 
            puts("");
    }

}
