#!/usr/bin/env python
import os
import matplotlib.pyplot as plt
from glob import glob
from collections import Counter, defaultdict

# ref: zhuozhiyongde/Introduction-to-Computer-System-2023Fall-PKU/blob/main/07-Malloc-Lab/trace-analysis.py

# 指针编号到大小的映射
block_index_map = {}  # 存储 block index 到 size 的映射
config_rep = [
    "amptjp-bal.rep",
    "cccp-bal.rep",
    "cp-decl-bal.rep",
    "expr-bal.rep",
    "coalescing-bal.rep",
    "random-bal.rep",
    "random2-bal.rep",
    "binary-bal.rep",
    "binary2-bal.rep",
    "realloc-bal.rep",
    "realloc2-bal.rep"
]

# 解析单个 trace 文件
def parse_trace_file(trace_path):
    with open(trace_path, "r") as file:
        lines = file.readlines()

    # 忽略非英文字符开头的行
    lines = [line for line in lines if line[0].isalpha()]

    operations = []  # 存储所有操作
    timestamp = 0    # 操作时间戳
    
    for line in lines:
        tokens = line.split()
        operation = tokens[0]
        
        # 处理分配/重分配操作
        if operation in ["a", "r"] and len(tokens) == 3:
            block_index = int(tokens[1])
            size = int(tokens[2])
            operations.append((timestamp, operation, size))
            timestamp += 1
            block_index_map[block_index] = size  # 更新映射
        
        # 处理释放操作
        elif operation == "f" and len(tokens) >= 2:
            block_index = int(tokens[1])
            # 检查 block_index 是否存在于映射中
            if block_index not in block_index_map:
                print(f"Warning: Free non-existent block {block_index} in {trace_path}")
                continue
            size = block_index_map[block_index]
            operations.append((timestamp, operation, size))
            timestamp += 1
            del block_index_map[block_index]  # 移除映射

    return operations


# # 绘制时间-大小关系图
def plot_operations(operations, plot_path):
    timestamps = [op[0] for op in operations]
    operation_types = [op[1] for op in operations]
    block_sizes = [op[2] for op in operations]

    color_map = {"a": "red", "r": "blue", "f": "green"}
    label_map = {
        "a": "Malloc",
        "r": "Realloc",
        "f": "Free"
    }
    
    plt.figure(figsize=(10, 6))
    
    # 为每种操作类型创建代理图例对象
    legend_proxies = []
    for op_type in ["a", "r", "f"]:
        if op_type in operation_types:
            proxy = plt.scatter([], [], s=30, color=color_map[op_type], 
                               label=label_map[op_type])
            legend_proxies.append(proxy)
    
    # 一次性绘制所有点
    scatter = plt.scatter(
        timestamps, 
        block_sizes, 
        s=2, 
        c=[color_map[op] for op in operation_types]
    )
    
    # 添加图例
    plt.legend(handles=legend_proxies, loc="best", fontsize=9)
    
    plt.xlabel("Time (Operation Sequence)")
    plt.ylabel("Block Size (Bytes)")
    file_name = os.path.basename(plot_path).split('.')[0]
    plt.title(f"Block Allocation/Release Timeline ({file_name})")
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    # 保存图像
    file_ext = plot_path.split(".")[-1]
    plt.savefig(plot_path, format=file_ext, dpi=200)
    plt.close()

# 统计不同大小的出现频率
def summarize_block_sizes(operations, summary_path, total_counter):
    # 只统计分配操作（不包括释放）
    alloc_sizes = [size for _, op, size in operations if op != "f"]
    size_counter = Counter(alloc_sizes)
    
    # 按块大小排序
    sorted_sizes = sorted(size_counter.items(), key=lambda x: x[0])
    
    # 更新总计数器
    for size, count in sorted_sizes:
        total_counter[size] += count
    
    # 写入摘要文件
    with open(summary_path, "w") as f:
        f.write("size,count\n")
        for size, count in sorted_sizes:
            f.write(f"{size},{count}\n")


# 主处理函数：处理 trace 目录
def process_trace_files(trace_dir):
    # 创建输出目录结构
    output_root = "trace-analysis"
    plot_dir = os.path.join(output_root, "plots")
    summary_dir = os.path.join(output_root, "summaries")
    os.makedirs(plot_dir, exist_ok=True)
    os.makedirs(summary_dir, exist_ok=True)

    # 全局统计计数器
    total_size_counter = defaultdict(int)
    
    # 获取所有 trace 文件
    trace_files = glob(os.path.join(trace_dir, "*.rep"))

    for trace_file in trace_files:
        if os.path.basename(trace_file) not in config_rep: continue
        # 每次处理新文件前重置映射
        block_index_map.clear()
        
        base_name = os.path.splitext(os.path.basename(trace_file))[0]
        plot_path = os.path.join(plot_dir, f"{base_name}.png")
        summary_path = os.path.join(summary_dir, f"{base_name}_summary.csv")
        
        # 处理单个 trace 文件
        print(f"Processing {os.path.basename(trace_file)}")
        operations = parse_trace_file(trace_file)
        plot_operations(operations, plot_path)
        summarize_block_sizes(operations, summary_path, total_size_counter)
    
    # 输出全局统计
    total_path = os.path.join(output_root, "total_summary.csv")
    with open(total_path, "w") as f:
        f.write("size,count\n")
        for size in sorted(total_size_counter):
            f.write(f"{size},{total_size_counter[size]}\n")


# 执行处理
if __name__ == "__main__":
    trace_directory = "traces"
    process_trace_files(trace_directory)