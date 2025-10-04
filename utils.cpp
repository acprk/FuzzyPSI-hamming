#include "utils.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>

void CommStats::print(const std::string& phase) const {
    std::cout << phase << " 通信统计:" << std::endl;
    std::cout << "  发送: " << getMegabytesSent() << " MB" << std::endl;
    std::cout << "  接收: " << getMegabytesReceived() << " MB" << std::endl;
    std::cout << "  总计: " << getTotalMegabytes() << " MB" << std::endl;
}

namespace utils {

std::vector<uint8_t> generateRandomBinaryVector(int d, PRNG& prng) {
    std::vector<uint8_t> vec(d);
    for (int i = 0; i < d; ++i) {
        vec[i] = prng.getBit();
    }
    return vec;
}

std::vector<uint8_t> generateVectorWithDistance(
    const std::vector<uint8_t>& base, int distance, PRNG& prng) {
    
    std::vector<uint8_t> vec = base;
    int d = vec.size();
    
    if (distance > d) {
        distance = d;
    }
    
    // 随机选择 distance 个位置进行翻转
    std::vector<int> positions(d);
    for (int i = 0; i < d; ++i) {
        positions[i] = i;
    }
    
    // Fisher-Yates 洗牌
    for (int i = d - 1; i > 0; --i) {
        int j = prng.get<uint32_t>() % (i + 1);
        std::swap(positions[i], positions[j]);
    }
    
    // 翻转前 distance 个位置
    for (int i = 0; i < distance; ++i) {
        vec[positions[i]] = 1 - vec[positions[i]];
    }
    
    return vec;
}

int hammingDistance(const std::vector<uint8_t>& v1, const std::vector<uint8_t>& v2) {
    int dist = 0;
    size_t minSize = std::min(v1.size(), v2.size());
    
    for (size_t i = 0; i < minSize; ++i) {
        if (v1[i] != v2[i]) {
            ++dist;
        }
    }
    
    return dist;
}

block vectorToBlock(const std::vector<uint8_t>& vec, int offset) {
    uint64_t low = 0, high = 0;
    
    for (int i = 0; i < 64 && (offset + i) < static_cast<int>(vec.size()); ++i) {
        if (vec[offset + i]) {
            low |= (1ULL << i);
        }
    }
    
    for (int i = 0; i < 64 && (offset + 64 + i) < static_cast<int>(vec.size()); ++i) {
        if (vec[offset + 64 + i]) {
            high |= (1ULL << i);
        }
    }
    
    return block(low, high);
}

std::vector<uint8_t> blockToVector(const block& b, int d) {
    std::vector<uint8_t> vec(d);
    
    uint64_t data[2];
    std::memcpy(data, &b, sizeof(block));
    uint64_t low = data[0];
    uint64_t high = data[1];
    
    for (int i = 0; i < 64 && i < d; ++i) {
        vec[i] = (low >> i) & 1;
    }
    
    for (int i = 0; i < 64 && (64 + i) < d; ++i) {
        vec[64 + i] = (high >> i) & 1;
    }
    
    return vec;
}

void printVector(const std::vector<uint8_t>& vec, const std::string& name) {
    if (!name.empty()) {
        std::cout << name << ": ";
    }
    
    for (size_t i = 0; i < std::min(vec.size(), size_t(20)); ++i) {
        std::cout << static_cast<int>(vec[i]);
    }
    
    if (vec.size() > 20) {
        std::cout << "...";
    }
    
    std::cout << " (size=" << vec.size() << ")" << std::endl;
}

void saveStats(const std::string& filename,
               const std::string& role,
               double offline_time,
               double online_time,
               const CommStats& offline_comm,
               const CommStats& online_comm,
               int n, int d, int delta) {
    
    std::ofstream file(filename, std::ios::app);
    if (!file.is_open()) {
        std::cerr << "无法打开文件: " << filename << std::endl;
        return;
    }
    
    file << "========================================" << std::endl;
    file << "角色: " << role << std::endl;
    file << "参数: n=" << n << ", d=" << d << ", δ=" << delta << std::endl;
    file << "========================================" << std::endl;
    file << std::endl;
    
    file << "离线阶段:" << std::endl;
    file << "  时间: " << offline_time << " 秒" << std::endl;
    file << "  发送: " << offline_comm.getMegabytesSent() << " MB" << std::endl;
    file << "  接收: " << offline_comm.getMegabytesReceived() << " MB" << std::endl;
    file << "  总计: " << offline_comm.getTotalMegabytes() << " MB" << std::endl;
    file << std::endl;
    
    file << "在线阶段:" << std::endl;
    file << "  时间: " << online_time << " 秒" << std::endl;
    file << "  发送: " << online_comm.getMegabytesSent() << " MB" << std::endl;
    file << "  接收: " << online_comm.getMegabytesReceived() << " MB" << std::endl;
    file << "  总计: " << online_comm.getTotalMegabytes() << " MB" << std::endl;
    file << std::endl;
    
    file << "总计:" << std::endl;
    file << "  时间: " << (offline_time + online_time) << " 秒" << std::endl;
    file << "  通信: " << (offline_comm.getTotalMegabytes() + online_comm.getTotalMegabytes()) 
         << " MB" << std::endl;
    file << std::endl;
    
    file.close();
}

} // namespace utils