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

速度 Kops: number of ops (malloc/free/realloc) in all trace / total time。


mdriver.c 中定义了 `REPEAT`，表示重复计算的次数。目的是多次测量取平均，提高测量时间的精度。