from collections import defaultdict
from pprint import pprint
import helper

op_count, malloc_dict, free_dict, realloc_dict = helper.parse_traces()
alloc_ops = op_count['malloc'] + op_count['realloc']  # alloc 次数

# 计算对齐后的asize->count字典
total_alloc_list = defaultdict(int)
for asize, count in malloc_dict.items():
    total_alloc_list[helper.align(asize)] += count
for asize, count in realloc_dict.items():
    total_alloc_list[helper.align(asize)] += count
    

# 按出现次数从小到大排序
asize_count_list = sorted(
    [(asize, count) for asize, count in total_alloc_list.items()],
    key = lambda x:x[1],
    reverse=True
)  # eg: [(16, 8800), (48, 8800), (56, 2200)]

top = 80
count_limit = 10
cumu_percent = 0.0
print("|  block size   | count | self % | cumulative % | idx |")
print("|---------------|-------|--------|--------------|-----|") # Alignments: Left, Right, Right, Right, Right


for i in range(len(asize_count_list)):
    
    cur_size, cur_count = asize_count_list[i]
    self_percent = (cur_count / alloc_ops) * 100
    cumu_percent += self_percent
    if cumu_percent >= top:
        break
    if cur_count <= count_limit:
        break
    print(
        f"| {cur_size:<13} "  # Left-aligned, 16 chars wide
        f"| {cur_count:>5} "  # Right-aligned, 5 chars wide
        f"| {self_percent:>6.1f} " # Right-aligned, 6 chars wide, 2 decimal places
        f"| {cumu_percent:>12.1f} " # Right-aligned, 12 chars wide, 2 decimal places
        f"| {i:>3} |" # Right-aligned, 3 chars wide
    )

print("The total number of size:", len(asize_count_list))