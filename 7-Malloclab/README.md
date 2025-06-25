### lab 框架文件更新

### 输出格式/多次测量取平均值
原始输出不够宽，数字挤到一起。
```
Results for mm malloc:
trace  valid  util     ops      secs  Kops
 0       yes   99%    5694  0.000057 99199
 1       yes   99%    5848  0.000057102417
 2       yes   99%    6648  0.000067 99076
 3       yes   99%    5380  0.000053102281
 4       yes   91%   14400  0.000104138462
 5       yes   96%    4800  0.000126 38156
 6       yes   94%    4800  0.000137 35036
 7       yes   92%   12000  0.000087137143
 8       yes   75%   24000  0.000195123014
 9       yes  100%   14401  0.000091157906
10       yes   98%   14401  0.000070206911
Total          95%  112372  0.001044107595
```

而且由于时间过于短，测试的时间误差较大。改为**多次测量求平均值**。

修改 mdriver.c：
```diff
@@ -26,6 +26,11 @@
+// 衡量时间时，执行的重复次数。
+// 可视化或调试时设置为1
+#define REPEAT 100

@@ -255,7 +260,10 @@ 
-      libc_stats[i].secs = fsecs(eval_libc_speed, &speed_params);
+      //& 修改框架代码：添加多次重复取平均值。
+      for (int r = 0; r < REPEAT; ++ r)
+          libc_stats[i].secs += fsecs(eval_libc_speed, &speed_params);
+      libc_stats[i].secs /= REPEAT;
```
```diff
@@ -892,25 +903,25 @@ 
static void printresults(int n, stats_t* stats) {
     double util = 0; 
     /* Print the individual results for each trace */
-    printf("%5s%7s %5s%8s%12s%8s\n", 
-          "trace", " valid", "util", "ops", "secs", "Kops");
+    printf("%5s%7s %5s%8s%12s%8s%12s\n", 
+          "trace", " valid", "util", "ops", "secs", "Kops", "total secs");
     for (i = 0; i < n; i++) {
         if (stats[i].valid) {
-            printf("%2d%10s%5.0f%%%8.0f%12.6f%8.0f\n", i, "yes", stats[i].util * 100.0,
-                   stats[i].ops, stats[i].secs, (stats[i].ops / 1e3) / stats[i].secs);
+            printf("%2d%10s%5.0f%%%8.0f%12.6f%8.0f%12.6f\n", i, "yes", stats[i].util * 100.0,
+                   stats[i].ops, stats[i].secs, (stats[i].ops / 1e3) / stats[i].secs, stats[i].secs * REPEAT);
             secs += stats[i].secs;
             ops += stats[i].ops;
             util += stats[i].util;
         } else {
-            printf("%2d%10s%6s%8s%12s%8s\n", i, "no", "-", "-", "-", "-");
+            printf("%2d%10s%6s%8s%12s%8s%12s\n", i, "no", "-", "-", "-", "-", "-");
         }
     }

     /* Print the aggregate results for the set of traces */
     if (errors == 0) {
-        printf("%12s%5.0f%%%8.0f%12.6f%8.0f\n", "Total       ", (util / n) * 100.0, ops, secs,
-               (ops / 1e3) / secs);
+        printf("%12s%5.0f%%%8.0f%12.6f%8.0f%12.6f\n", "Total       ", (util / n) * 100.0, ops, secs,
+               (ops / 1e3) / secs, secs * REPEAT);
     } else {
-        printf("%12s%6s%8s%12s%8s\n", "Total       ", "-", "-", "-", "-");
+        printf("%12s%6s%8s%12s%8s%12s\n", "Total       ", "-", "-", "-", "-", "-");
     }
 }
```


#### 添加编译选项 debug, no-inline
修改 makefile，添加 debug 模式，no-inline 模式，两者可组合：
```makefile
CC = gcc-11
# 可去除-m32，一样通过测试。
CFLAGS = -Wall -O3 -m32 -march=native  -fopt-info-vec-optimized
DFLAGS = -g -Wall -O0 -m32 -mno-avx # 调试标志：保留符号表，禁用优化
NOINLINE_FLAG = -fno-inline  # 禁止内联，方便调试和性能测量


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

```



### 测评
速度单位 Kops: number of ops (malloc/free/realloc) in all trace / total time。



测试：
```sh
make clean && make && ./mdriver -v  # 需要glibc的速度加 -l 
```


### 分析 `./mdriver -v` 输出示例
```sh
Results for mm malloc:
trace  valid  util     ops        secs    Kops  total secs
 0       yes   99%    5694    0.000038  150127    0.003793
 1       yes  100%    5848    0.000036  163142    0.003585
 2       yes  100%    6648    0.000043  156236    0.004255
 3       yes  100%    5380    0.000035  152936    0.003518
 4       yes   97%   14400    0.000074  194083    0.007420
 5       yes   94%    4800    0.000074   64914    0.007394
 6       yes   94%    4800    0.000077   62022    0.007739
 7       yes   97%   12000    0.000061  197791    0.006067
 8       yes   90%   24000    0.000127  188578    0.012727
 9       yes  100%   14401    0.000060  241190    0.005971
10       yes   99%   14401    0.000045  323582    0.004451
Total          97%  112372    0.000669  167924    0.066918
```

每个编号代表的**测试文件**名定义在 `config.h`，一共 11 个：
```C
#define DEFAULT_TRACEFILES \
  "amptjp-bal.rep",\        0
  "cccp-bal.rep",\          1 
  "cp-decl-bal.rep",\       2
  "expr-bal.rep",\          3
  "coalescing-bal.rep",\    4
  "random-bal.rep",\        5
  "random2-bal.rep",\       6
  "binary-bal.rep",\        7
  "binary2-bal.rep",\       8
  "realloc-bal.rep",\       9
  "realloc2-bal.rep"        10
```

`mdriver.c` 中这段代码指定了默认使用 `config.h:TRACEDIR` 下的文件：
```c
if (tracefiles == NULL) {
    tracefiles = default_tracefiles;
    num_tracefiles = sizeof(default_tracefiles) / sizeof(char*) - 1;
    printf("Using default tracefiles in %s\n", tracedir);
}
```

