from typing import List, Tuple, Dict

class BucketNode:
    def __init__(self, key=None, left=None, right=None, bucket_id=None):
        self.key = key        # 分割值（仅内部节点）
        self.left = left      # 左子树（< key）
        self.right = right    # 右子树（> key）
        self.bucket_id = bucket_id  # 桶ID（仅叶节点）
        
    def is_leaf(self):
        return self.bucket_id is not None

def build_optimized_bst(boundaries: List[Tuple[int, int]], 
                        frequencies: List[float]) -> BucketNode:
    """
    针对分桶方案构建最优二叉搜索树，考虑特殊比较成本
    
    参数:
        boundaries: 桶边界列表，如[(7,16), (24,104), ...]
        frequencies: 每个桶的访问频率
    
    返回:
        最优二叉搜索树的根节点
    """
    n = len(boundaries)
    
    # 1. 计算分割点：桶之间的间隙
    split_points = []
    for i in range(len(boundaries) - 1):
        prev_end = boundaries[i][1]
        next_start = boundaries[i+1][0]
        # 对于不连续的桶边界，使用中点作为分割点
        if next_start > prev_end + 1:
            split_value = (prev_end + next_start) // 2
            split_points.append((i, split_value))
    
    # 2. 计算前缀和（用于快速计算子树概率）
    total_freq = sum(frequencies)
    prefix = [0.0] * (n + 1)
    for i in range(n):
        prefix[i+1] = prefix[i] + frequencies[i] / total_freq
    
    # 3. DP表初始化
    # dp[i][j] = (min_cost, best_root_index) for buckets i to j
    # cost[i][j] 表示桶区间[i, j]的最小搜索成本
    dp = [[0.0] * n for _ in range(n)]
    root = [[-1] * n for _ in range(n)]
    
    # 4. 动态规划填表
    # 考虑特殊的成本模型：左子树成本最低，右子树成本最高
    for length in range(1, n+1):
        for i in range(n - length + 1):
            j = i + length - 1
            
            min_cost = float('inf')
            best_root = -1
            
            for r in range(i, j+1):
                # 子区间概率和（用于成本计算）
                p_sum = prefix[j+1] - prefix[i]
                
                # 当前桶的概率
                bucket_prob = frequencies[r] / total_freq
                
                # 成本计算：考虑特殊的比较路径
                left_cost = dp[i][r-1] if r > i else 0.0
                right_cost = dp[r+1][j] if r < j else 0.0
                
                # 根据成本模型调整：
                # 左子树：仅需要1次比较（无额外成本）
                # 相等：需要2次比较（左比较失败+相等检查）
                # 右子树：需要3次比较（左比较失败+相等检查失败+右子树比较）
                # 右子树额外+1成本：左比较失败后的额外比较
                cost = p_sum + bucket_prob * 2  # 相等检查的基础成本
                
                # 添加子树成本（考虑路径成本）
                cost += left_cost
                cost += right_cost + (prefix[j+1] - prefix[r+1])  # 右子树额外成本
                
                if cost < min_cost:
                    min_cost = cost
                    best_root = r
            
            dp[i][j] = min_cost
            root[i][j] = best_root
    
    # 5. 重构最优树结构
    def build_tree(i: int, j: int) -> BucketNode:
        if i > j:
            return None
        
        r = root[i][j]
        
        # 找到该桶对应的分割点（如果没有现成的，使用桶边界）
        split_val = boundaries[r][1]  # 使用桶的结束值作为分割点
        
        # 创建节点
        node = BucketNode(key=split_val)
        node.left = build_tree(i, r-1)
        node.right = build_tree(r+1, j)
        
        # 为叶节点设置桶ID
        if node.left is None and node.right is None:
            node.bucket_id = r
        
        return node
    
    return build_tree(0, n-1),

# 测试示例
if __name__ == "__main__":
    # 分桶方案
    boundaries = [(16, 24), (32, 112), (120, 128), (136, 136), (144, 456), (464, 4072), (4080, 4080), (4096, 4104), (4112, 8552), (8560, 20032), (20040, 95112), (95240, 614792)]


    
    # 桶访问频率
    loads = [9337, 3198, 4004, 8857, 3084, 2767, 8697, 4806, 4057, 4058, 4060, 4060]
    
    # 构建最优二叉搜索树
    root= build_optimized_bst(boundaries, loads)
    
    # 打印树结构（简单版本）
    def print_tree(node, level=0, prefix="Root: "):
        if node is None:
            return
        indent = "    " * level
        if node.is_leaf():
            print(f"{indent}{prefix}Bucket {node.bucket_id} (Range: {boundaries[node.bucket_id]})")
        else:
            print(f"{indent}{prefix}Compare with {node.key}")
            print_tree(node.left, level+1, "Left: ")
            print_tree(node.right, level+1, "Right: ")
    
    print_tree(root)
    