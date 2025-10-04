#pragma once

#include <vector>
#include <set>
#include <string>
#include <cstdint>
#include "cryptoTools/Common/Defines.h"
#include "cryptoTools/Crypto/PRNG.h"

using namespace osuCrypto;

class ELSHFmap {
public:
    // 构造函数
    ELSHFmap(int d, int delta, int L, double tau = 0.5);
    
    // 计算单个向量的 ID 集合
    std::set<std::string> computeID(const std::vector<uint8_t>& vector);
    
    // 批量计算 ID 集合
    std::vector<std::set<std::string>> computeIDBatch(
        const std::vector<std::vector<uint8_t>>& vectors);
    
    // 获取参数
    int getD() const { return d_; }
    int getDelta() const { return delta_; }
    int getL() const { return L_; }
    int getK() const { return k_; }
    
    // 获取子集（用于调试）
    const std::vector<std::vector<int>>& getSubsets() const { return subsets_; }

private:
    int d_;           // 向量维度
    int delta_;       // Hamming 距离阈值
    int L_;           // 哈希函数数量
    double tau_;      // 熵阈值
    int k_;           // 子集大小
    
    std::vector<int> high_entropy_dims_;        // 高熵维度
    std::vector<std::vector<int>> subsets_;     // L 个随机子集
    
    // 选择高熵维度
    void selectHighEntropyDimensions();
    
    // 生成随机子集
    void generateRandomSubsets();
};