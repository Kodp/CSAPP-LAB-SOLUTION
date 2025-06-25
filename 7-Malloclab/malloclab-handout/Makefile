#
# Students' Makefile for the Malloc Lab
#
TEAM = bovik
VERSION = 1
HANDINDIR = /afs/cs.cmu.edu/academic/class/15213-f01/malloclab/handin

CC = gcc-11
# 可去除-m32，一样通过测试。
CFLAGS = -Wall -O3 -m32 -march=native  -fopt-info-vec-optimized
DFLAGS = -g -Wall -O0 -m32 -mno-avx # 调试标志：保留符号表，禁用优化
NOINLINE_FLAG = -fno-inline -g -fno-omit-frame-pointer  
# 禁止内联，方便调试和性能测量
# -fno-omit-frame-pointer  保留帧指针

OBJS = mdriver.o mm.o memlib.o fsecs.o fcyc.o clock.o ftimer.o

all: mdriver

# 编译 debug 模式
debug: CFLAGS = $(DFLAGS) # 替换 CFLAGS 为 DFLAGS
debug: clean mdriver
	@echo "=== 构建了调试版本（含调试符号） ==="

# 禁止内联的优化版本
# 使用 += 将 -fno-inline 追加到现有的 CFLAGS 中
noinline: CFLAGS += -fno-inline
noinline: clean mdriver
	@echo "=== 构建了禁止内联的优化版本 ==="

# 调试+禁止内联的组合版本
debug-noinline: CFLAGS = $(DFLAGS) $(NOINLINE_FLAG)
debug-noinline: clean mdriver
	@echo "=== 构建了调试+禁止内联的组合版本 ==="

mdriver: $(OBJS)
	$(CC) $(CFLAGS) -o mdriver $(OBJS) -lm

mdriver.o: mdriver.c fsecs.h fcyc.h clock.h memlib.h config.h mm.h
memlib.o: memlib.c memlib.h
mm.o: mm.c mm.h memlib.h log.h
fsecs.o: fsecs.c fsecs.h config.h
fcyc.o: fcyc.c fcyc.h	
ftimer.o: ftimer.c ftimer.h config.h
clock.o: clock.c clock.h

handin:
	cp mm.c $(HANDINDIR)/$(TEAM)-$(VERSION)-mm.c

clean:
	rm -f *~ *.o mdriver


