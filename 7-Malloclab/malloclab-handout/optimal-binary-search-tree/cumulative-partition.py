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

# 按块从小到大排序
asize_count_list = sorted(
    [(asize, count) for asize, count in total_alloc_list.items()],
    key = lambda x:x[0],
    reverse=False
)  # eg: [(16, 8800), (24, 5), (32, 20)]

expected_buckets = int(input("Input the expected number of buckets:"))
segment_count = alloc_ops // expected_buckets
print(f"Expect {expected_buckets} buckets, with {segment_count} elements expected per bucket")
bucket_ranges = []      # 块大小区间：[(0, 19), (20, 40)]
bucket_counts = []  # 区间出现次数：[553,  1000]
sum_count = 0  # 累积出现次数
left_asize = 0  # 上一个区间的左端点

for i, (asize, count) in enumerate(asize_count_list):
    # 如果积累了一桶标准容量，则当前size为分界点：
    # 分为 [last_size, size-1] 和 [size, ...]
    if sum_count >= segment_count:
        bucket_ranges.append((left_asize, asize - 1))
        bucket_counts.append(sum_count)
        sum_count = 0
        left_asize = asize
    sum_count += count

bucket_ranges.append((left_asize, -1))
bucket_counts.append(sum_count)
assert(sum(bucket_counts) == alloc_ops)  # 确保划分不重不漏
print(f"The actual number of buckets: {len(bucket_ranges)}")

cumu_percent = 0.0
print("| bucket range     | count | self % | cumulative % | idx |")
print("|------------------|-------|--------|--------------|-----|") # Alignments: Left, Right, Right, Right, Right

for i in range(len(bucket_ranges)):
    range_str = f"[{bucket_ranges[i][0]},{bucket_ranges[i][1]}]"
    current_count = bucket_counts[i]
    # Calculate percentages
    self_percent = (current_count / alloc_ops) * 100
    cumu_percent += self_percent

    print(
        f"| {range_str:<16} "  # Left-aligned, 20 chars wide
        f"| {current_count:>5} "  # Right-aligned, 5 chars wide
        f"| {self_percent:>6.1f} " # Right-aligned, 6 chars wide, 2 decimal places
        f"| {cumu_percent:>12.1f} " # Right-aligned, 12 chars wide, 2 decimal places
        f"| {i:>3} |" # Right-aligned, 3 chars wide
    )

