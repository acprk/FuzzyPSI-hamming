#include "elsh.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

ELSHFmap::ELSHFmap(int d, int delta, int L, double tau)
    : d_(d), delta_(delta), L_(L), tau_(tau) {
    
    // 计算 k = ceil(d / (delta + 1))
    k_ = static_cast<int>(std::ceil(static_cast<double>(d) / (delta + 1)));
    
    std::cout << "E-LSH 参数配置:" << std::endl;
    std::cout << "  维度 d = " << d_ << std::endl;
    std::cout << "  阈值 δ = " << delta_ << std::endl;
    std::cout << "  子集大小 k = " << k_ << std::endl;
    std::cout << "  哈希函数数量 L = " << L_ << std::endl;
    
    // 选择高熵维度
    selectHighEntropyDimensions();
    
    // 生成随机子集
    generateRandomSubsets();
    
    std::cout << "  高熵维度数量 = " << high_entropy_dims_.size() << std::endl;
    std::cout << std::endl;
}

void ELSHFmap::selectHighEntropyDimensions() {
    // 模拟熵计算：假设所有维度都是高熵
    // 在实际应用中，应该根据数据集统计计算熵
    high_entropy_dims_.reserve(d_);
    
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> dist(0.4, 0.6);
    
    std::vector<std::pair<double, int>> entropy_dims;
    
    for (int i = 0; i < d_; ++i) {
        double p = dist(rng);
        p = std::max(0.01, std::min(0.99, p));
        
        // 计算熵 H = -p*log2(p) - (1-p)*log2(1-p)
        double entropy = -p * std::log2(p) - (1 - p) * std::log2(1 - p);
        
        entropy_dims.push_back({entropy, i});
    }
    
    // 按熵值排序
    std::sort(entropy_dims.begin(), entropy_dims.end(), 
              std::greater<std::pair<double, int>>());
    
    // 选择高熵维度
    for (const auto& [entropy, dim] : entropy_dims) {
        if (entropy > tau_ || high_entropy_dims_.size() < static_cast<size_t>(k_ * L_)) {
            high_entropy_dims_.push_back(dim);
        }
    }
    
    // 确保至少有 k*L 个维度
    if (high_entropy_dims_.size() < static_cast<size_t>(k_ * L_)) {
        for (int i = 0; i < d_ && high_entropy_dims_.size() < static_cast<size_t>(k_ * L_); ++i) {
            if (std::find(high_entropy_dims_.begin(), high_entropy_dims_.end(), i) 
                == high_entropy_dims_.end()) {
                high_entropy_dims_.push_back(i);
            }
        }
    }
}

void ELSHFmap::generateRandomSubsets() {
    std::mt19937 rng(42);
    subsets_.resize(L_);
    
    for (int l = 0; l < L_; ++l) {
        std::vector<int> subset;
        subset.reserve(k_);
        
        // 从高熵维度中随机选择 k 个
        std::vector<int> candidates = high_entropy_dims_;
        std::shuffle(candidates.begin(), candidates.end(), rng);
        
        int size = std::min(k_, static_cast<int>(candidates.size()));
        for (int i = 0; i < size; ++i) {
            subset.push_back(candidates[i]);
        }
        
        subsets_[l] = subset;
    }
}

std::set<std::string> ELSHFmap::computeID(const std::vector<uint8_t>& vector) {
    std::set<std::string> ids;
    
    for (int l = 0; l < L_; ++l) {
        int parity = 0;
        
        for (int dim : subsets_[l]) {
            if (dim < static_cast<int>(vector.size())) {
                parity ^= static_cast<int>(vector[dim]);
            }
        }
        
        // 构造 ID 字符串: "l||parity"
        std::string id = std::to_string(l) + "||" + std::to_string(parity);
        ids.insert(id);
    }
    
    return ids;
}

std::vector<std::set<std::string>> ELSHFmap::computeIDBatch(
    const std::vector<std::vector<uint8_t>>& vectors) {
    
    std::vector<std::set<std::string>> result;
    result.reserve(vectors.size());
    
    for (const auto& vec : vectors) {
        result.push_back(computeID(vec));
    }
    
    return result;
}