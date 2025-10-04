import numpy as np
from collections import defaultdict
from typing import List, Set, Tuple
import os
from datetime import datetime

# 尝试导入 matplotlib，如果失败则禁用可视化
try:
    import matplotlib
    matplotlib.use('Agg')  # 使用非交互式后端
    import matplotlib.pyplot as plt
    # 设置中文字体
    plt.rcParams['font.sans-serif'] = ['SimHei', 'DejaVu Sans']
    plt.rcParams['axes.unicode_minus'] = False
    MATPLOTLIB_AVAILABLE = True
except ImportError as e:
    print(f"Warning: matplotlib 不可用 ({e})")
    print("可视化功能将被禁用，但其他功能正常运行")
    MATPLOTLIB_AVAILABLE = False


class ELSHFmap:
    """E-LSH Fmap 协议实现"""
    
    def __init__(self, d: int, delta: int, L: int, tau: float = 0.5):
        """
        初始化 E-LSH Fmap 参数
        
        Args:
            d: 向量维度
            delta: Hamming 距离阈值
            L: 哈希函数数量
            tau: 熵阈值
        """
        self.d = d
        self.delta = delta
        self.L = L
        self.tau = tau
        self.k = int(np.ceil(d / (delta + 1)))
        
        # 预计算：选择高熵维度
        self.high_entropy_dims = self._select_high_entropy_dimensions()
        
        # 生成 L 个随机子集（公共随机性）
        self.subsets = self._generate_random_subsets()
        
        print(f"参数配置:")
        print(f"  维度 d = {d}")
        print(f"  阈值 δ = {delta}")
        print(f"  子集大小 k = {self.k}")
        print(f"  哈希函数数量 L = {L}")
        print(f"  高熵维度数量 = {len(self.high_entropy_dims)}")
        print()
    
    def _select_high_entropy_dimensions(self) -> List[int]:
        """选择高熵维度"""
        # 模拟熵计算：假设接近均匀分布
        entropies = []
        for i in range(self.d):
            p_i = 0.5 + np.random.uniform(-0.1, 0.1)
            p_i = np.clip(p_i, 0.01, 0.99)
            H_i = -p_i * np.log2(p_i) - (1 - p_i) * np.log2(1 - p_i)
            entropies.append(H_i)
        
        high_entropy_dims = [i for i, H in enumerate(entropies) if H > self.tau]
        
        if len(high_entropy_dims) < self.k:
            high_entropy_dims = sorted(range(self.d), 
                                      key=lambda i: entropies[i], 
                                      reverse=True)[:max(self.k * self.L, self.d)]
        
        return high_entropy_dims
    
    def _generate_random_subsets(self) -> List[List[int]]:
        """生成 L 个随机子集，每个大小为 k"""
        np.random.seed(42)
        subsets = []
        
        for l in range(self.L):
            subset = np.random.choice(self.high_entropy_dims, 
                                     size=min(self.k, len(self.high_entropy_dims)), 
                                     replace=False)
            subsets.append(subset.tolist())
        
        return subsets
    
    def compute_id(self, vector: np.ndarray) -> Set[str]:
        """计算向量的 ID 集合"""
        ids = set()
        
        for l, subset in enumerate(self.subsets):
            parity = 0
            for i in subset:
                parity ^= int(vector[i])
            
            id_str = f"{l}||{parity}"
            ids.add(id_str)
        
        return ids
    
    def compute_id_batch(self, vectors: np.ndarray) -> List[Set[str]]:
        """批量计算向量的 ID"""
        return [self.compute_id(v) for v in vectors]


def hamming_distance(v1: np.ndarray, v2: np.ndarray) -> int:
    """计算两个二进制向量的 Hamming 距离"""
    return np.sum(v1 != v2)


def generate_test_data_balanced(n: int, d: int, delta: int) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
    """
    生成更平衡的测试数据，包含各种距离的向量对
    
    Args:
        n: 每个集合的向量数量
        d: 向量维度
        delta: 目标 Hamming 距离阈值
        
    Returns:
        (Q, W, distances)
    """
    # 生成 Sender 的向量集合
    Q = np.random.randint(0, 2, size=(n, d))
    
    # 生成 Receiver 的向量集合
    W = np.zeros((n, d), dtype=int)
    
    # 更细致的分布：
    # 20% 距离 <= delta/2
    # 20% 距离在 (delta/2, delta]
    # 20% 距离在 (delta, delta*1.5]
    # 20% 距离在 (delta*1.5, delta*2]
    # 20% 随机距离
    
    splits = [int(n * 0.2), int(n * 0.4), int(n * 0.6), int(n * 0.8), n]
    
    for i in range(n):
        base_idx = i % len(Q)  # 使用模运算确保每个Q向量都被使用
        W[i] = Q[base_idx].copy()
        
        if i < splits[0]:
            # 距离 <= delta/2
            num_flips = np.random.randint(0, max(1, delta // 2))
        elif i < splits[1]:
            # 距离在 (delta/2, delta]
            num_flips = np.random.randint(max(1, delta // 2), delta + 1)
        elif i < splits[2]:
            # 距离在 (delta, delta*1.5]
            num_flips = np.random.randint(delta + 1, int(delta * 1.5) + 1)
        elif i < splits[3]:
            # 距离在 (delta*1.5, delta*2]
            num_flips = np.random.randint(int(delta * 1.5) + 1, delta * 2 + 1)
        else:
            # 随机距离
            num_flips = np.random.randint(0, d)
        
        num_flips = min(num_flips, d)
        if num_flips > 0:
            flip_positions = np.random.choice(d, size=num_flips, replace=False)
            W[i][flip_positions] = 1 - W[i][flip_positions]
    
    # 计算所有配对的 Hamming 距离
    distances = np.zeros((n, n), dtype=int)
    for i in range(n):
        for j in range(n):
            distances[i, j] = hamming_distance(Q[i], W[j])
    
    return Q, W, distances


def save_vectors_to_txt(vectors: np.ndarray, filename: str, id_sets: List[Set[str]] = None):
    """
    将向量保存到 txt 文件
    
    Args:
        vectors: 向量数组
        filename: 输出文件名
        id_sets: 可选的 ID 集合列表
    """
    with open(filename, 'w', encoding='utf-8') as f:
        f.write(f"# Total vectors: {len(vectors)}\n")
        f.write(f"# Dimension: {vectors.shape[1]}\n")
        f.write("#" + "="*70 + "\n\n")
        
        for i, vec in enumerate(vectors):
            f.write(f"Vector {i}:\n")
            # 将向量写成一行，用空格分隔
            vec_str = ' '.join(map(str, vec))
            f.write(f"{vec_str}\n")
            
            if id_sets is not None:
                f.write(f"ID Set: {sorted(id_sets[i])}\n")
            
            f.write("\n")


def plot_results(close_pairs, far_pairs, delta, d, output_dir, timestamp, n):
    """可视化实验结果"""
    if not MATPLOTLIB_AVAILABLE:
        return
    
    fig, axes = plt.subplots(1, 2, figsize=(14, 5))
    
    # 图1: Hamming 距离分布
    ax1 = axes[0]
    close_dists = [dist for dist, _ in close_pairs]
    far_dists = [dist for dist, _ in far_pairs]
    
    # 使用更多的bins来显示更细致的分布
    bins = min(50, d // 2)
    
    if close_dists:
        ax1.hist(close_dists, bins=bins, alpha=0.7, label=f'Distance <= {delta}', color='blue', edgecolor='black')
    if far_dists:
        ax1.hist(far_dists, bins=bins, alpha=0.7, label=f'Distance > {delta}', color='red', edgecolor='black')
    
    ax1.axvline(x=delta, color='green', linestyle='--', linewidth=2, label=f'Threshold delta={delta}')
    ax1.set_xlabel('Hamming Distance', fontsize=12)
    ax1.set_ylabel('Number of Pairs', fontsize=12)
    ax1.set_title('Hamming Distance Distribution', fontsize=14, fontweight='bold')
    ax1.legend(fontsize=10)
    ax1.grid(True, alpha=0.3)
    
    # 图2: 检测率 vs Hamming 距离
    ax2 = axes[1]
    all_pairs = close_pairs + far_pairs
    
    # 按距离分组统计检测率
    dist_detection = defaultdict(lambda: {'total': 0, 'detected': 0})
    for dist, has_int in all_pairs:
        dist_detection[dist]['total'] += 1
        if has_int:
            dist_detection[dist]['detected'] += 1
    
    distances = sorted(dist_detection.keys())
    detection_rates = [100 * dist_detection[d]['detected'] / dist_detection[d]['total'] 
                      for d in distances]
    
    ax2.plot(distances, detection_rates, 'o-', markersize=5, linewidth=2, color='blue')
    ax2.axvline(x=delta, color='green', linestyle='--', linewidth=2, label=f'Threshold delta={delta}')
    ax2.axhline(y=50, color='gray', linestyle=':', alpha=0.5)
    ax2.set_xlabel('Hamming Distance', fontsize=12)
    ax2.set_ylabel('Detection Rate (%)', fontsize=12)
    ax2.set_title('ID Intersection Detection Rate vs Hamming Distance', fontsize=14, fontweight='bold')
    ax2.legend(fontsize=10)
    ax2.grid(True, alpha=0.3)
    ax2.set_ylim(-5, 105)
    
    plt.tight_layout()
    
    # 保存图片
    img_file = os.path.join(output_dir, f'elsh_results_n{n}_d{d}_delta{delta}_{timestamp}.png')
    plt.savefig(img_file, dpi=200, bbox_inches='tight')
    print(f"可视化结果已保存到: {img_file}")
    
    plt.close()


def run_experiment(n: int, d: int = 128, delta: int = 10, L: int = 32, 
                   output_dir: str = "/home/luck/xzy/ac/Fmap"):
    """运行完整实验"""
    
    # 创建输出目录
    os.makedirs(output_dir, exist_ok=True)
    
    # 创建子目录用于存放不同规模的实验
    exp_dir = os.path.join(output_dir, f"n{n}_d{d}_delta{delta}")
    os.makedirs(exp_dir, exist_ok=True)
    
    # 创建日志文件
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_file = os.path.join(exp_dir, f"experiment.log")
    
    def log_print(msg):
        """同时打印到控制台和日志文件"""
        print(msg)
        with open(log_file, 'a', encoding='utf-8') as f:
            f.write(msg + '\n')
    
    log_print("=" * 70)
    log_print("E-LSH Fmap 协议实验验证")
    log_print("=" * 70)
    log_print(f"实验目录: {exp_dir}")
    log_print(f"数据规模: n={n} (2^{int(np.log2(n))})")
    log_print(f"时间戳: {timestamp}")
    log_print("")
    
    # 初始化协议
    elsh = ELSHFmap(d=d, delta=delta, L=L)
    
    # 将参数信息写入日志
    log_print("参数配置:")
    log_print(f"  维度 d = {d}")
    log_print(f"  阈值 δ = {delta}")
    log_print(f"  子集大小 k = {elsh.k}")
    log_print(f"  哈希函数数量 L = {L}")
    log_print(f"  高熵维度数量 = {len(elsh.high_entropy_dims)}")
    log_print("")
    
    # 生成测试数据
    log_print("正在生成测试数据...")
    Q, W, distances = generate_test_data_balanced(n, d, delta)
    log_print(f"已生成 {n} 个 Sender 向量和 {n} 个 Receiver 向量\n")
    
    # 保存生成的数据
    data_file = os.path.join(exp_dir, f"test_data.npz")
    np.savez(data_file, Q=Q, W=W, distances=distances)
    log_print(f"测试数据已保存到: {data_file}\n")
    
    # 计算 ID
    log_print("正在计算 Sender 的 ID 集合...")
    ID_Q = elsh.compute_id_batch(Q)
    
    log_print("正在计算 Receiver 的 ID 集合...")
    ID_W = elsh.compute_id_batch(W)
    log_print("")
    
    # 保存原始向量和 ID 到 txt 文件
    log_print("正在保存数据到 txt 文件...")
    
    # Sender 原始向量
    sender_raw_file = os.path.join(exp_dir, "sender_raw_vectors.txt")
    save_vectors_to_txt(Q, sender_raw_file)
    log_print(f"  Sender 原始向量已保存: {sender_raw_file}")
    
    # Sender ID 集合
    sender_id_file = os.path.join(exp_dir, "sender_id_sets.txt")
    save_vectors_to_txt(Q, sender_id_file, ID_Q)
    log_print(f"  Sender ID 集合已保存: {sender_id_file}")
    
    # Receiver 原始向量
    receiver_raw_file = os.path.join(exp_dir, "receiver_raw_vectors.txt")
    save_vectors_to_txt(W, receiver_raw_file)
    log_print(f"  Receiver 原始向量已保存: {receiver_raw_file}")
    
    # Receiver ID 集合
    receiver_id_file = os.path.join(exp_dir, "receiver_id_sets.txt")
    save_vectors_to_txt(W, receiver_id_file, ID_W)
    log_print(f"  Receiver ID 集合已保存: {receiver_id_file}")
    log_print("")
    
    # 验证结果
    log_print("=" * 70)
    log_print("验证结果")
    log_print("=" * 70)
    
    # 统计数据
    true_positives = 0
    false_negatives = 0
    true_negatives = 0
    false_positives = 0
    
    close_pairs = []
    far_pairs = []
    
    # 保存详细的匹配结果
    match_results = []
    
    for i in range(n):
        for j in range(n):
            dist = distances[i, j]
            has_intersection = len(ID_Q[i] & ID_W[j]) > 0
            intersection_size = len(ID_Q[i] & ID_W[j])
            
            match_results.append({
                'sender_idx': i,
                'receiver_idx': j,
                'hamming_distance': dist,
                'has_intersection': has_intersection,
                'intersection_size': intersection_size
            })
            
            if dist <= delta:
                close_pairs.append((dist, has_intersection))
                if has_intersection:
                    true_positives += 1
                else:
                    false_negatives += 1
            else:
                far_pairs.append((dist, has_intersection))
                if not has_intersection:
                    true_negatives += 1
                else:
                    false_positives += 1
    
    # 保存匹配结果到 CSV
    import csv
    results_file = os.path.join(exp_dir, f"match_results.csv")
    with open(results_file, 'w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=['sender_idx', 'receiver_idx', 
                                                'hamming_distance', 'has_intersection', 
                                                'intersection_size'])
        writer.writeheader()
        writer.writerows(match_results)
    log_print(f"匹配结果已保存到: {results_file}\n")
    
    total_close = true_positives + false_negatives
    total_far = true_negatives + false_positives
    
    log_print(f"\n近距离配对 (Hamming 距离 ≤ {delta}):")
    log_print(f"  总数: {total_close}")
    log_print(f"  正确检测 (有交集): {true_positives} ({100*true_positives/max(1,total_close):.2f}%)")
    log_print(f"  漏检 (无交集): {false_negatives} ({100*false_negatives/max(1,total_close):.2f}%)")
    
    log_print(f"\n远距离配对 (Hamming 距离 > {delta}):")
    log_print(f"  总数: {total_far}")
    log_print(f"  正确拒绝 (无交集): {true_negatives} ({100*true_negatives/max(1,total_far):.2f}%)")
    log_print(f"  误检 (有交集): {false_positives} ({100*false_positives/max(1,total_far):.2f}%)")
    
    # 理论预测
    rho = delta / d
    p_single = (1 + (1 - 2*rho)**elsh.k) / 2
    p_collision = 1 - (1 - p_single)**L
    
    log_print(f"\n理论预测:")
    log_print(f"  归一化距离 ρ = δ/d = {rho:.4f}")
    log_print(f"  单哈希碰撞概率 p = {p_single:.4f}")
    log_print(f"  至少一次碰撞概率 = {p_collision:.6f}")
    log_print(f"  预期检测率 ≥ {100*p_collision:.4f}%")
    
    # 分析不同距离下的检测率
    log_print(f"\n按距离分组的检测率:")
    distance_ranges = [(0, delta//2), (delta//2, delta), (delta, delta*2), (delta*2, d)]
    for low, high in distance_ranges:
        pairs_in_range = [(d, h) for d, h in close_pairs + far_pairs if low <= d < high]
        if pairs_in_range:
            detected = sum(1 for _, h in pairs_in_range if h)
            log_print(f"  距离 [{low}, {high}): {detected}/{len(pairs_in_range)} = {100*detected/len(pairs_in_range):.2f}%")
    
    # 保存统计摘要
    summary_file = os.path.join(exp_dir, f"summary.txt")
    with open(summary_file, 'w', encoding='utf-8') as f:
        f.write("E-LSH Fmap 实验统计摘要\n")
        f.write("=" * 50 + "\n\n")
        f.write(f"数据规模: n={n} (2^{int(np.log2(n))})\n")
        f.write(f"参数: d={d}, δ={delta}, k={elsh.k}, L={L}\n\n")
        if total_close > 0:
            f.write(f"近距离配对 (≤ {delta}): {true_positives}/{total_close} = {100*true_positives/total_close:.2f}%\n")
        if total_far > 0:
            f.write(f"远距离配对 (> {delta}): {true_negatives}/{total_far} = {100*true_negatives/total_far:.2f}%\n")
        f.write(f"\n理论预测检测率: {100*p_collision:.4f}%\n")
        if total_close > 0:
            f.write(f"实际检测率: {100*true_positives/total_close:.2f}%\n")
    log_print(f"\n统计摘要已保存到: {summary_file}")
    
    # 可视化
    if MATPLOTLIB_AVAILABLE:
        plot_results(close_pairs, far_pairs, delta, d, exp_dir, timestamp, n)
    else:
        log_print("\n注意: matplotlib 不可用，跳过可视化步骤")
    
    log_print("\n" + "=" * 70)
    log_print("实验完成！")
    log_print(f"所有结果已保存到: {exp_dir}")
    log_print("=" * 70 + "\n")
    
    return {
        'true_positives': true_positives,
        'false_negatives': false_negatives,
        'true_negatives': true_negatives,
        'false_positives': false_positives,
        'total_close': total_close,
        'total_far': total_far
    }


def run_all_experiments(output_dir: str = "/home/luck/xzy/ac/Fmap"):
    """运行所有规模的实验"""
    
    # 实验配置
    d = 128
    delta = 10
    L = 32
    
    # 测试不同规模: 2^8, 2^10, 2^12, 2^14, 2^16
    sizes = [2**i for i in [8, 10, 12, 14, 16]]
    
    print("=" * 80)
    print("开始运行多规模实验")
    print("=" * 80)
    print(f"参数: d={d}, δ={delta}, L={L}")
    print(f"测试规模: {[f'2^{int(np.log2(n))}={n}' for n in sizes]}")
    print("=" * 80)
    print()
    
    results_summary = []
    
    for n in sizes:
        print(f"\n{'='*80}")
        print(f"运行实验: n = 2^{int(np.log2(n))} = {n}")
        print(f"{'='*80}\n")
        
        result = run_experiment(n=n, d=d, delta=delta, L=L, output_dir=output_dir)
        results_summary.append({
            'n': n,
            'log2_n': int(np.log2(n)),
            **result
        })
    
    # 保存总体摘要
    summary_file = os.path.join(output_dir, "all_experiments_summary.txt")
    with open(summary_file, 'w', encoding='utf-8') as f:
        f.write("E-LSH Fmap 多规模实验总结\n")
        f.write("=" * 80 + "\n\n")
        f.write(f"参数: d={d}, δ={delta}, L={L}\n\n")
        f.write(f"{'规模':<15} {'近距离检测率':<20} {'远距离拒绝率':<20}\n")
        f.write("-" * 80 + "\n")
        
        for res in results_summary:
            close_rate = 100 * res['true_positives'] / max(1, res['total_close'])
            far_rate = 100 * res['true_negatives'] / max(1, res['total_far'])
            f.write(f"2^{res['log2_n']} = {res['n']:<6}   {close_rate:>6.2f}%              {far_rate:>6.2f}%\n")
    
    print("\n" + "=" * 80)
    print("所有实验完成！")
    print(f"总体摘要已保存到: {summary_file}")
    print("=" * 80)


if __name__ == "__main__":
    # 运行所有规模的实验
    run_all_experiments(output_dir="/home/luck/xzy/ac/Fmap")