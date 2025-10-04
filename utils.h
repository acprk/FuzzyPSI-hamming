#pragma once

#include <vector>
#include <chrono>
#include <string>
#include <cstdint>
#include "cryptoTools/Common/Defines.h"
#include "cryptoTools/Crypto/PRNG.h"

using namespace osuCrypto;

// 时间统计类
class Timer {
public:
    void start() {
        start_time_ = std::chrono::high_resolution_clock::now();
    }
    
    void stop() {
        end_time_ = std::chrono::high_resolution_clock::now();
    }
    
    double getElapsedSeconds() const {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time_ - start_time_);
        return duration.count() / 1000000.0;
    }
    
    double getElapsedMilliseconds() const {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time_ - start_time_);
        return duration.count() / 1000.0;
    }

private:
    std::chrono::high_resolution_clock::time_point start_time_;
    std::chrono::high_resolution_clock::time_point end_time_;
};

// 通信统计类
class CommStats {
public:
    void addSent(uint64_t bytes) { bytes_sent_ += bytes; }
    void addReceived(uint64_t bytes) { bytes_received_ += bytes; }
    
    uint64_t getBytesSent() const { return bytes_sent_; }
    uint64_t getBytesReceived() const { return bytes_received_; }
    uint64_t getTotalBytes() const { return bytes_sent_ + bytes_received_; }
    
    double getMegabytesSent() const { return bytes_sent_ / (1024.0 * 1024.0); }
    double getMegabytesReceived() const { return bytes_received_ / (1024.0 * 1024.0); }
    double getTotalMegabytes() const { return getTotalBytes() / (1024.0 * 1024.0); }
    
    void reset() {
        bytes_sent_ = 0;
        bytes_received_ = 0;
    }
    
    void print(const std::string& phase) const;

private:
    uint64_t bytes_sent_ = 0;
    uint64_t bytes_received_ = 0;
};

// 工具函数
namespace utils {
    // 生成随机二进制向量
    std::vector<uint8_t> generateRandomBinaryVector(int d, PRNG& prng);
    
    // 生成指定 Hamming 距离的向量
    std::vector<uint8_t> generateVectorWithDistance(
        const std::vector<uint8_t>& base, int distance, PRNG& prng);
    
    // 计算 Hamming 距离
    int hammingDistance(const std::vector<uint8_t>& v1, const std::vector<uint8_t>& v2);
    
    // 将向量转换为 block
    block vectorToBlock(const std::vector<uint8_t>& vec, int offset = 0);
    
    // 将 block 转换为向量
    std::vector<uint8_t> blockToVector(const block& b, int d);
    
    // 打印向量（用于调试）
    void printVector(const std::vector<uint8_t>& vec, const std::string& name = "");
    
    // 保存统计信息到文件
    void saveStats(const std::string& filename, 
                   const std::string& role,
                   double offline_time,
                   double online_time,
                   const CommStats& offline_comm,
                   const CommStats& online_comm,
                   int n, int d, int delta);
}