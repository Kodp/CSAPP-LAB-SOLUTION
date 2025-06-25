import os
import matplotlib
from collections import defaultdict
# 设置Matplotlib使用中文字体
try:
    matplotlib.rcParams['font.family'] = 'WenQuanYi Zen Hei'
    matplotlib.rcParams['axes.unicode_minus'] = False
except Exception as e:
    print(f"无法设置中文字体，使用默认字体。错误: {e}")
    matplotlib.rcParams['font.family'] = 'sans-serif'


allowed_filenames = [
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

def identity(n):
    return n

def align(n):
    def ALIGN(size):
        return (size + 7) & ~0x7
    if n == 112 + 8:
        return ALIGN(128 + 8)
    if n == 448 + 8:
        return ALIGN(512 + 8)
    return ALIGN(n + 8)

# traces文件的路径
traces_path = "../traces"

def parse_traces(directory=traces_path):
    """
    解析指定目录下的所有.rep文件。
    返回统计字典:
    op_count: {malloc: 25, free: 30, realloc: 25}
    malloc_dict: {7: 1, 16: 25}
    """
    # 初始化统计字典
    malloc_dict = defaultdict(int)
    realloc_dict = defaultdict(int)
    free_dict = defaultdict(int)
    op_count = {'malloc': 0, 'free': 0, 'realloc': 0}
    block_map = {}  # 存储块大小映射
    
    for filename in os.listdir(directory):
        if filename not in allowed_filenames:
            continue
                
        filepath = os.path.join(directory, filename)
        
        with open(filepath, 'r') as f:
            lines = f.readlines()
        
        # 跳过文件头的元数据行
        operations = lines[4:]
        
        for op_line in operations:
            tokens = op_line.split()
            if not tokens:
                continue
            op_type = tokens[0]
            
            # 处理分配操作 (malloc)
            if op_type == 'a':
                seq, size = int(tokens[1]), int(tokens[2])
                op_count['malloc'] += 1
                malloc_dict[size] += 1
                block_map[seq] = size
                
            # 处理释放操作 (free)
            elif op_type == 'f':
                seq = int(tokens[1])
                if seq not in block_map:
                    continue
                
                op_count['free'] += 1
                size = block_map[seq]
                free_dict[size] += 1
                del block_map[seq]
                
            # 处理重分配操作 (realloc)
            elif op_type == 'r':
                old_seq, new_size = int(tokens[1]), int(tokens[2])
                if old_seq not in block_map:
                    continue 
                op_count['realloc'] += 1
                realloc_dict[new_size] += 1
                block_map[old_seq] = new_size

    return op_count, malloc_dict, free_dict, realloc_dict