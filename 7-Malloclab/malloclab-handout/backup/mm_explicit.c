//@ TRACEDIR 修改在 config.h, 还可以修改一些别的
// C语言中，指针类型转换本身没有运行时开销
/*
Measuring performance with gettimeofday().

Results for mm malloc:
trace  valid  util     ops      secs  Kops
 0       yes   93%    5694  0.000064 88416
 1       yes   95%    5848  0.000062 94628
 2       yes   95%    6648  0.000092 72576
 3       yes   97%    5380  0.000066 81392
 4       yes   99%   14400  0.000050287425
 5       yes   89%    4800  0.000145 33172
 6       yes   85%    4800  0.000160 29981
 7       yes   54%   12000  0.006716  1787
 8       yes   47%   24000  0.020963  1145
 9       yes   20%   14401  0.022966   627
10       yes   26%   14401  0.000523 27551
Total          73%  112372  0.051807  2169
*/
#define _POSIX_C_SOURCE 200809L  
#include "mm.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include "memlib.h"

team_t team = {"ateam", "Kodp", "123@456.com", "", ""};

#define print_orange(fmt, ...) printf("\033[38;5;208m" fmt "\033[0m", ##__VA_ARGS__)
#define print_green(fmt, ...) printf("\033[32m"fmt"\033[0m", ##__VA_ARGS__)

// 函数定义
void* find_fit(unsigned asize);
void* extend_heap(unsigned n);
void  place(void* pt, unsigned asize);
void* coalesce(void* pt);
void* remove_piece(char* pt);
void set_piece(char* pt, size_t size, int alloc, char* next, char* prev); 
void print_piece(void* pt, char* base);
void print_heap();
void print_freelist();
int freelist_len();
void mm_check();
void check_uncoalesce();
void check_free();
void check_prev_next();
void sigsegv_handler(int sig);

// -----------------------------------------------------------------------
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define SSIZE sizeof(size_t)  // 4
#define MINSIZE 16            // 最小块大小 | 元数据大小

// 参数必须是指针
#define GETU(p)  (*(unsigned*)(p))
#define GETP(p)  (*(uintptr_t*)(p))
#define SIZE(p)  (GETU(p) & ~0x7)   
#define ALLOC(p) (GETU(p) & 0x1)
#define PUT(p, val) (*(unsigned*)(p) = (val))

// NEXTP, PREVP 给出指针地址；
#define NEXTP(p)  ((char*)(p) + 1 * SSIZE)   // next的位置
#define PREVP(p)  ((char*)(p) + 2 * SSIZE)   // prev的位置
#define PAYLOADP(p) ((char*)(p) + 3 * SSIZE)   // 数据指针
#define FOOTP(p)  ((char*)(p) + SIZE(p) - SSIZE)

// NEXT, PREV 给出指针；
#define NEXT(p)  (GETP(NEXTP(p)))        // 空闲链表下一节点的起始地址
#define PREV(p)  (GETP(PREVP(p)))        // 空闲链表上一节点的起始地址
#define MEMNEXT(p) ((char*)p + SIZE(p))  // 内存下一块的起始地址

char* head, *tail; // 指向序言块，尾块

/**
功能：初始化堆：初始化head、tail节点。
返回：初始化状态，0为成功，-1为失败。

head<->tail，size=16、已分配。
*/
int mm_init(void) { 
    // 注册 SIGSEGV 处理器
    struct sigaction sa;
    sa.sa_handler = sigsegv_handler;  // 设置自定义的处理器函数
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;  // 确保信号处理后重新启动被中断的系统调用
    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("Error: cannot handle SIGSEGV");
        exit(1);
    }

    mem_sbrk(SSIZE);  // 用于对齐的块
    head = mem_sbrk(MINSIZE);
    tail = mem_sbrk(MINSIZE);
    if (head == (void*) -1 || tail == (void*) - 1) return -1;
    set_piece(head, MINSIZE, 1, tail, NULL);
    set_piece(tail, MINSIZE, 1, NULL, head);
    return 0;
}

/**
尝试分配一个块，其中数据区域（block）空间>=n。

这个块要去**空闲链表**里找。
- 能**找到满足空间大小**的：分配
- 否则，需要**添加一个新块**，新块的大小就是需要的空间大小。
- 放置新块。

返回 `block` 地址。
*/
void* mm_malloc(size_t n) {
    if (n <= 0) return NULL;
    unsigned asize = ALIGN(n + MINSIZE);  // n+head+foot+next+prev
    void* pt;
    if ((pt = find_fit(asize)) != NULL) {
        place(pt, asize);
        return PAYLOADP(pt);
    }
    else if ((pt = extend_heap(asize)) != NULL) {
        place(pt, asize);
        return PAYLOADP(pt);
    }
    return NULL;
}


/**
> `a` 表示 adjusted，修改过的 `size` 值。
> ”修改”表示“*添加了固定开销且向上对齐到 8 的倍数*”。

在空闲链表里查找是否存在块空间大小>=asize。
从 head 的下一个块开始访问，遍历到 tail 之前停止。（不访问 head 和 tail）
*/
void* find_fit(unsigned asize) {
    void* pt = NEXT(head);
    while (pt != tail) {
        if (SIZE(pt) >= asize) 
            return pt;
        pt = NEXT(pt);
    }
    return NULL;
}

/**
功能：扩展堆空间、添加一个 asize 大小的新块。
返回：新块的起始指针。

首先调用堆函数，分配 asize 空间，拿到堆顶指针 brk。如果堆空间不够，返回 `NULL`。

新块的位置：
tail 的位置我们永远维护在堆顶，那么需要把当前的新块放置在 tail 的位置，然后把 tail 迁移到堆顶；

分配后，尝试合并空闲块。
*/
void* extend_heap(unsigned asize) {
    char* pt;
    if ((pt = mem_sbrk(asize)) == (void*)-1) {
        // 内存用尽，检查堆的问题：
        mm_check();
        return NULL;
    }
    pt = pt - MINSIZE;  // 移动到tail块的位置
    // 先设置新快；因为还要用tail的prev，不能先设置tail
    // pt.next = newtail, pt.prev = oldtail.prev
    set_piece(pt, asize, 0, pt + asize, PREV(tail));  
    // 无须设置oldtail.prev->next由原来的oldtail改为指向pt，因为pt已经鸠占鹊巢，占了之前tail的位置。
    tail = pt + asize;
    // 设置tail
    set_piece(tail, MINSIZE, 1, NULL, pt);  // tail.prev=pt

    return coalesce(pt);
}


/**
功能：将一个空闲块pt转化为一个已分配块、切出后半块作为空闲块。

pt 表示要求传进来的指针是块的头指针，不是 block 的指针。
在 pt 指向的块的中（前部）尝试切出 `asize` 大小的块为已分配块，把后头剩余的作为一个空闲块链接回空闲链表；
如果大小刚好则不切。（调用要保证 pt 块空间必须>= asize、传入空闲块）
*/
void place(void* pt, unsigned asize) {  // 空闲块
    unsigned have_size = SIZE(pt);
    assert(ALLOC(pt) == 0);  // 必须传入空闲块
    assert(have_size >= asize);  // 空闲块大小必须大于等于asize

    // 空间足够大，可以切割
    if (have_size - asize > MINSIZE) {
        char* free_piece = (char*)pt + asize;
        //@ 原地插
        set_piece(free_piece, have_size - asize, 0, NEXT(pt), PREV(pt));
        // 将空闲链表中的pt块“换”成free_piece块：让pt的前后连接到free_piece
        PUT(NEXTP(PREV(pt)), free_piece); // pt.prev->next = free_piece
        PUT(PREVP(NEXT(pt)), free_piece);  // pt.next->prev = free_piece
        // 此时pt块“被动”离开空闲链表；设为已分配
        set_piece(pt, asize, 1, NULL, NULL);
    }  // 空间刚能满足
    else {
        // pt块“主动”离开空闲链表
        remove_piece(pt);
    }
}



/**
功能：释放 payload 块、重新链接回空闲链表。

可以直接放回链表的头部，也就是让 head 指向它（LIFO）。
- 对 payload 做偏移，移动到块的开始，命名 pt。
- 设置 pt 的 size_head 和 size_foot 的 alloc 为 false(0)
- 链接回空闲链表的头部。(不代表物理地址移动到了头部！)
- 尝试合并。
 */
void mm_free(void* payload) {
    char* pt = (char*)payload - 3 * SSIZE;
    // 头插
    set_piece(pt, SIZE(pt), 0, NEXT(head), head);  
    PUT(NEXTP(head), pt);
    PUT(PREVP(NEXT(pt)), pt);
    int status;
    coalesce(pt);
}


/**
功能：重分配块。 
*/
void* mm_realloc(void* payload, size_t size) {
    if (payload == NULL) {
        return mm_malloc(size);
    }
    if (size == 0) {
        mm_free(payload);
        return NULL;
    }
    char* pt = (char*)payload - 3 * SSIZE;  // 指向块的起始位置
    char* new_pt;
    
    size_t oldsize = SIZE(pt);
    size_t asize = ALIGN(size + MINSIZE);
    size_t copy_size = oldsize - MINSIZE; // 数据区大小
    // 情况1：新大小<=现有大小（缩小）
    if (oldsize >= asize) {
        // 已分配块转换为已分配块，只需要设置大小
        if (oldsize - asize > MINSIZE) {
            PUT(pt, asize | 1);
            PUT(FOOTP(pt), asize | 1);
            char* free_piece = (char*)pt + asize;
            set_piece(free_piece, oldsize - asize, 0, NEXT(head), head); // 剩余块插入空闲链表
            PUT(NEXTP(head), free_piece);
            PUT(PREVP(NEXT(free_piece)), free_piece);
        }
        return payload;
    }
    // 情况2:新大小>现有大小 查找空闲块使用
    else if ((new_pt = find_fit(asize)) != NULL) {
        place(new_pt, asize); //  设置新块
        memmove(PAYLOADP(new_pt), payload, copy_size);  // 复制数据
        mm_free(payload);  //  删除原块
        return PAYLOADP(new_pt);
    }
    // 情况3：需要分配新空间并复制数据
    else {
        char* new_bp = mm_malloc(size);
        if (new_bp == NULL) return NULL;
        // 计算复制数据的大小
        if (size < copy_size) copy_size = size;
        memmove(new_bp, payload, copy_size);
        mm_free(payload);
        return new_bp;
    }
}

/**
功能：尝试合并空闲块 pt 的前后块。
返回：合并后的块的起始指针。

前中合并：更新前块大小（head foot），删除中块
    设置前的 size 增大，中的末尾 size 增大；
后中合并：更新中块大小（head foot），删除后块
    设置中的 size 增大，中的末尾 size 增大；
前中后合并：更新前块大小（head foot），删除中块、后块
（可以用一个删除节点的函数）。（既可以添加在前块的后面，也可以添加在后块的后面，也可以**放在链表头**）
    设置前的 size 增大，前的末尾的 size 增大；
*/
 void* coalesce(void* pt) {
    assert(ALLOC(pt) == 0);  // 必须传入空闲块

    unsigned size = SIZE(pt);
    char* memprev_foot = (char*)pt - SSIZE;
    char* memprev_head = (char*)pt - SIZE(memprev_foot);
    char* memnext_head = (char*)pt + size;
    char* memnext_foot = FOOTP(memnext_head);
    unsigned memprev_alloc = ALLOC(memprev_foot);
    unsigned memnext_alloc = ALLOC(memnext_head);
    unsigned memnext_size = SIZE(memnext_head);
    unsigned memprev_size = SIZE(memprev_head);

    if (!memprev_alloc && memnext_alloc) {  
        // 内存上块为空闲块
        PUT(memprev_head, memprev_size + size);  
        //& 此时foot位置变了,不可使用之前的memprev_foot
        PUT(FOOTP(memprev_head), memprev_size + size);
        remove_piece(pt);
        return memprev_head;
    }
    else if (memprev_alloc && !memnext_alloc) {
        // 设置中块容量
        PUT(pt, size + memnext_size);  
        PUT(FOOTP(pt), size + memnext_size);
        remove_piece(memnext_head);
        return pt;
    }
    else if (!memprev_alloc && !memnext_alloc) {
        PUT(memprev_head, memprev_size + size + memnext_size);
        //& 此时foot位置变了,不可使用之前的memprev_foot
        PUT(FOOTP(memprev_head), memprev_size + size + memnext_size);
        remove_piece(pt);
        remove_piece(memnext_head);
        return memprev_head;
    }
    else {
        return pt;
    }
}

// 从空闲链表中移除pt块（设为分配块）
 void* remove_piece(char* pt){
    PUT(NEXTP(PREV(pt)), NEXT(pt));  // pt.prev->next = pt.next
    PUT(PREVP(NEXT(pt)), PREV(pt));  // pt.next->prev = pt.prev
    PUT(NEXTP(pt), NULL);
    PUT(PREVP(pt), NULL);
    unsigned size = SIZE(pt);
    PUT(pt, size | 1);
    PUT(pt + size - SSIZE, size | 1);
}

// 设置一个块的所有元数据。
void set_piece(char* pt, size_t size, int alloc, char* next, char* prev) {
    PUT(pt, size | alloc);
    PUT(pt + size - SSIZE, size | alloc);
    PUT(NEXTP(pt), next);
    PUT(PREVP(pt), prev);
}


// 检查堆情况（目前还不好用）
void mm_check() {
    check_prev_next();
    check_uncoalesce();
    check_free();
    print_green("Check succeed.\n");
}


// 正序遍历从 head 开始，倒序遍历从 tail 开始。
// 正序遍历检查当前 pt->next->prev 是否是自己；
// 倒序遍历检查当前 pt->prev->next 是否是自己。
void check_prev_next() {
    char* pt = head;
    while (pt != tail) {
        if (NEXT(pt) != tail && PREV(NEXT(pt)) != pt) {
            printf("Error: Inconsistency in linked list at %p.\n\
            Expected 'next' of the previous node to be current node. \n\
            Found: prev(next(p)) (%p) vs. next (%p)\n", pt, PREV(NEXT(pt)), NEXT(pt));
            print_heap();
            // print_freelist();
            exit(1);
        }
        pt = NEXT(pt);
    }

    pt = tail;
    while (pt != head) {
        if (PREV(pt) != head && NEXT(PREV(pt)) != pt) {
            printf("Error: Inconsistency in linked list at %p.\
            Expected 'prev' of the next node to be current node. \
            Found: next(prev(p)) (%p) vs. previous (%p)\n", pt, NEXT(PREV(pt)), PREV(pt));
            print_heap();
            exit(1);
        }
        pt = PREV(pt);
    }
}


// 从 head 开始连续内存遍历堆，每次检测当前块和下一块是否同时为未分配 alloc 0。
// 当当前块的下一块为 tail（pt->next 为空）停止。要有顺序的检查，防止 next->next segfault。
void check_uncoalesce() {
    char* brk = (char*)(mem_heap_lo()) + 1;
    char* pt = head;
    while(pt + SIZE(pt) < brk) {
        if (!ALLOC(pt) && !ALLOC(MEMNEXT(pt))) {
            printf("Error: uncoalesce error at %p and %p\n", pt, MEMNEXT(pt));
            printf("head: %d, %d\n", GETU(pt), GETU(MEMNEXT(pt)));
            printf("head: %p, tail: %p\n", head, tail);
            printf("brk:  %p\n", brk);
            exit(1);
        }
        pt = MEMNEXT(pt);
    }
}


// 遍历空闲链表，检查 alloc 位置。`assert(get_alloc(pt) == 0)`
void check_free() {
    char* pt = NEXT(head);
    while(pt != tail) {
        if (ALLOC(pt) || ALLOC(FOOTP(pt))) {
            printf("Error: free error at %p\n", pt);
            printf("head: %p, tail: %p\n", head, tail);
            exit(1);
        }
        pt = NEXT(pt);
    }
}

void print_piece(void* pt, char* base) {
    printf("Addr: %5p", (char*)pt - base);
    printf(", HD: [%5d:%c]", SIZE(pt), ALLOC(pt) ? 'a' : 'f');
    printf(", NE: %5p", (char*)(NEXT(pt)) == NULL ? NULL : (char*)(NEXT(pt)) - base);
    printf(", PR: %5p", (char*)(PREV(pt)) == NULL ? NULL : (char*)(PREV(pt)) - base);
    printf(", FT: [%d:%c]", SIZE(FOOTP(pt)), ALLOC(FOOTP(pt)) ? 'a' : 'f');
    puts("");
}

void print_heap() {
   puts("────────────────────────────── HEAP↓ ────────────────────────────────");
   char* brk = (char*)(mem_heap_lo()) + 1;
   char* base = mem_heap_lo();
   printf("base: %p\n", brk);
   char* pt = head;
   int count = 0;
   while ((char*)pt < brk) {
        print_piece(pt, base);
        pt += SIZE(pt);
        count ++;
        if (count > 20000) {
            printf("Error: infinite loop in print_heap!");
            exit(1);
        }
   }
   puts("────────────────────────────── HEAP↑ ────────────────────────────────");
}

void print_freelist() {
    puts("──────────────────────────── FREELIST↓ ──────────────────────────────");
    char* pt = head;
    char* base = mem_heap_lo();
    while (pt != tail) {
        print_piece(pt, base);
        pt = NEXT(pt);
    }
    puts("──────────────────────────── FREELIST↑ ──────────────────────────────");
}

int freelist_len() {
    char* pt = head;
    int len = 0;
    while (pt != tail) {
        pt = NEXT(pt);
        len ++;
    }
    return len;
}

void sigsegv_handler(int sig) {
    printf("Caught signal %d: Segmentation Fault\n", sig);
    printf("    head: %p, tail: %p\n", head, tail);
    mm_check();
    fflush(stdout);

    print_heap();
    exit(1);  // 在捕获到段错误后，退出程序
}