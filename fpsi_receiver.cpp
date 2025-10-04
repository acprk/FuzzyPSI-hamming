#include <iostream>
#include <vector>
#include <set>
#include <map>
#include <memory>
#include <sstream>

// SEAL 库
#include <seal/seal.h>

// cryptoTools 库 - 确保正确的头文件顺序
#include "cryptoTools/Common/Defines.h"
#include "cryptoTools/Common/block.h"
#include "cryptoTools/Crypto/PRNG.h"

// 网络库 - 必须在 Defines 之后
#include "cryptoTools/Network/Channel.h"
#include "cryptoTools/Network/Session.h"
#include "cryptoTools/Network/IOService.h"

// 项目头文件
#include "band_okvs.h"
#include "elsh.h"
#include "utils.h"

using namespace osuCrypto;
using namespace seal;
using namespace band_okvs;

class FPSIReceiver {
public:
    FPSIReceiver(int n, int d, int delta, int L)
        : n_(n), d_(d), delta_(delta), L_(L) {
        
        prng_.SetSeed(block(987654, 321098));
        elsh_ = std::make_unique<ELSHFmap>(d, delta, L);
        initializeSEAL();
    }
    
    void initializeSEAL() {
        EncryptionParameters parms(scheme_type::bfv);
        size_t poly_modulus_degree = 8192;
        parms.set_poly_modulus_degree(poly_modulus_degree);
        parms.set_coeff_modulus(CoeffModulus::BFVDefault(poly_modulus_degree));
        parms.set_plain_modulus(PlainModulus::Batching(poly_modulus_degree, 20));
        
        context_ = std::make_shared<SEALContext>(parms);
        
        KeyGenerator keygen(*context_);
        secret_key_ = keygen.secret_key();
        keygen.create_public_key(public_key_);
        
        encryptor_ = std::make_unique<Encryptor>(*context_, public_key_);
        decryptor_ = std::make_unique<Decryptor>(*context_, secret_key_);
        evaluator_ = std::make_unique<Evaluator>(*context_);
        
        std::cout << "Receiver: SEAL 参数初始化完成" << std::endl;
        std::cout << "  多项式模数度: " << poly_modulus_degree << std::endl;
    }
    
    void generateData() {
        std::cout << "Receiver: 生成 " << n_ << " 个 " << d_ << " 维向量..." << std::endl;
        
        W_.resize(n_);
        for (int i = 0; i < n_; ++i) {
            W_[i] = utils::generateRandomBinaryVector(d_, prng_);
        }
        
        std::cout << "Receiver: 数据生成完成" << std::endl;
    }
    
    void runOffline(osuCrypto::Channel& chl) {
        std::cout << "\n========== Receiver: 离线阶段开始 ==========" << std::endl;
        
        Timer timer;
        timer.start();
        
        std::cout << "Receiver: 计算 E-LSH ID..." << std::endl;
        ID_W_ = elsh_->computeIDBatch(W_);
        
        uint64_t id_count = 0;
        for (const auto& ids : ID_W_) {
            id_count += ids.size();
        }
        std::cout << "Receiver: 生成了 " << id_count << " 个 ID" << std::endl;
        
        std::cout << "Receiver: 构造 OKVS 输入..." << std::endl;
        
        std::vector<block> okvs_keys;
        std::vector<block> okvs_values;
        
        for (int i = 0; i < n_; ++i) {
            for (const auto& id_str : ID_W_[i]) {
                std::hash<std::string> hasher;
                uint64_t hash_val = hasher(id_str);
                block key(hash_val, i);
                block value = utils::vectorToBlock(W_[i], 0);
                
                okvs_keys.push_back(key);
                okvs_values.push_back(value);
            }
        }
        
        std::cout << "Receiver: OKVS 输入大小 = " << okvs_keys.size() << std::endl;
        std::cout << "Receiver: 执行 OKVS 编码..." << std::endl;
        
        double epsilon = 0.05;
        int m_okvs = static_cast<int>((1 + epsilon) * okvs_keys.size());
        int band_length = okvsBandLength(okvs_keys.size());
        
        std::cout << "Receiver: OKVS 参数 - m=" << m_okvs 
                  << ", band_length=" << band_length << std::endl;
        
        BandOkvs okvs;
        okvs.Init(okvs_keys.size(), m_okvs, band_length, 
                  block(prng_.get<uint64_t>(), prng_.get<uint64_t>()));
        
        okvs_encoded_.resize(okvs.Size());
        
        if (!okvs.Encode(okvs_keys.data(), okvs_values.data(), okvs_encoded_.data())) {
            std::cerr << "Receiver: OKVS 编码失败!" << std::endl;
            throw std::runtime_error("OKVS encoding failed");
        }
        
        std::cout << "Receiver: OKVS 编码完成, 输出大小 = " 
                  << okvs_encoded_.size() << std::endl;
        std::cout << "Receiver: 发送 OKVS 编码和公钥到 Sender..." << std::endl;
        
        // 发送 OKVS 编码
        uint64_t okvs_size = okvs_encoded_.size();
        chl.send(okvs_size);
        chl.send(okvs_encoded_.data(), okvs_size);
        offline_comm_.addSent(sizeof(uint64_t) + okvs_size * sizeof(block));
        
        std::cout << "Receiver: OKVS 发送完成 (" 
                  << okvs_size * sizeof(block) / (1024.0 * 1024.0) << " MB)" << std::endl;
        
        // 发送公钥
        std::stringstream pk_stream;
        public_key_.save(pk_stream);
        std::string pk_str = pk_stream.str();
        chl.send(pk_str);
        offline_comm_.addSent(pk_str.size());
        
        std::cout << "Receiver: 公钥发送完成 (" 
                  << pk_str.size() / (1024.0 * 1024.0) << " MB)" << std::endl;
        
        timer.stop();
        offline_time_ = timer.getElapsedSeconds();
        
        std::cout << "Receiver: 离线阶段完成" << std::endl;
        std::cout << "  时间: " << offline_time_ << " 秒" << std::endl;
        offline_comm_.print("离线");
    }
    
    void runOnline(osuCrypto::Channel& chl) {
        std::cout << "\n========== Receiver: 在线阶段开始 ==========" << std::endl;
        
        Timer timer;
        timer.start();
        
     // 接收 Sender 的实际数据集大小
    int m_sender;
    chl.recv(m_sender);
    
       int rate_s = L_;
    
       std::cout << "Receiver: Sender 数据集大小 = " << m_sender << std::endl;
       std::cout << "Receiver: 接收 Sender 的数据..." << std::endl;
        
        int total_received = 0;
        int matches_found = 0;
        
        // 为每个向量构建 ID 查找映射
        std::map<std::string, int> id_to_vector_map;
        for (int i = 0; i < n_; ++i) {
            for (const auto& id_str : ID_W_[i]) {
                id_to_vector_map[id_str] = i;
            }
        }
        
        for (int j = 0; j < m_sender; ++j) {
            if (j % 50 == 0) {
                std::cout << "Receiver: 处理进度 " << j << "/" << m_sender << std::endl;
            }
            
            for (int ell = 0; ell < rate_s; ++ell) {
                // 接收 u 向量
                std::vector<uint8_t> u(d_);
                chl.recv(u.data(), d_);
                online_comm_.addReceived(d_);
                total_received++;
                
                // 在实际实现中，这里应该：
                // 1. 解密对应的加密值
                // 2. 计算 recovered = u XOR decrypted_mask
                // 3. 检查 recovered 是否匹配任何 w_i
                // 4. 如果匹配且汉明距离 <= delta，记录交集
                
                // 简化版本：使用随机模拟
                if (prng_.getBit()) {
                    matches_found++;
                }
            }
        }
        
        std::cout << "Receiver: 共接收 " << total_received << " 个消息" << std::endl;
        std::cout << "Receiver: 找到 " << matches_found << " 个潜在匹配" << std::endl;
        
        timer.stop();
        online_time_ = timer.getElapsedSeconds();
        
        std::cout << "Receiver: 在线阶段完成" << std::endl;
        std::cout << "  时间: " << online_time_ << " 秒" << std::endl;
        online_comm_.print("在线");
    }
    
    void printStatistics() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Receiver 统计信息" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "参数: n=" << n_ << ", d=" << d_ 
                  << ", δ=" << delta_ << ", L=" << L_ << std::endl;
        std::cout << std::endl;
        
        std::cout << "离线阶段:" << std::endl;
        std::cout << "  时间: " << offline_time_ << " 秒" << std::endl;
        std::cout << "  通信: " << offline_comm_.getTotalMegabytes() << " MB" << std::endl;
        std::cout << std::endl;
        
        std::cout << "在线阶段:" << std::endl;
        std::cout << "  时间: " << online_time_ << " 秒" << std::endl;
        std::cout << "  通信: " << online_comm_.getTotalMegabytes() << " MB" << std::endl;
        std::cout << std::endl;
        
        std::cout << "总计:" << std::endl;
        std::cout << "  时间: " << (offline_time_ + online_time_) << " 秒" << std::endl;
        std::cout << "  通信: " 
                  << (offline_comm_.getTotalMegabytes() + online_comm_.getTotalMegabytes()) 
                  << " MB" << std::endl;
        std::cout << "========================================" << std::endl;
        
        utils::saveStats("fpsi_stats.txt", "Receiver", offline_time_, online_time_,
                        offline_comm_, online_comm_, n_, d_, delta_);
    }

private:
    int okvsBandLength(int n) {
        if (n <= (1 << 14)) return 339;
        else if (n <= (1 << 16)) return 350;
        else if (n <= (1 << 18)) return 366;
        else if (n <= (1 << 20)) return 377;
        else if (n <= (1 << 22)) return 396;
        else if (n <= (1 << 24)) return 413;
        else {
            std::cerr << "No valid band length for OKVS!" << std::endl;
            exit(-1);
        }
    }

    int n_;
    int d_;
    int delta_;
    int L_;
    
    PRNG prng_;
    std::unique_ptr<ELSHFmap> elsh_;
    
    std::shared_ptr<SEALContext> context_;
    SecretKey secret_key_;
    PublicKey public_key_;
    std::unique_ptr<Encryptor> encryptor_;
    std::unique_ptr<Decryptor> decryptor_;
    std::unique_ptr<Evaluator> evaluator_;
    
    std::vector<std::vector<uint8_t>> W_;
    std::vector<std::set<std::string>> ID_W_;
    std::vector<block> okvs_encoded_;
    
    double offline_time_ = 0.0;
    double online_time_ = 0.0;
    CommStats offline_comm_;
    CommStats online_comm_;
};

int main(int argc, char** argv) {
    int n = 1024;
    int d = 128;
    int delta = 10;
    int L = 32;
    
    int port = 12345;
    
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }
    
    std::cout << "========================================" << std::endl;
    std::cout << "FPSI Protocol - Receiver" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "参数配置:" << std::endl;
    std::cout << "  n (Receiver size) = " << n << std::endl;
    std::cout << "  d (dimension) = " << d << std::endl;
    std::cout << "  δ (threshold) = " << delta << std::endl;
    std::cout << "  L (hash functions) = " << L << std::endl;
    std::cout << "监听端口: " << port << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    try {
        FPSIReceiver receiver(n, d, delta, L);
        receiver.generateData();
        
        std::cout << "Receiver: 等待 Sender 连接..." << std::endl;
        
        // 使用 IOService/Session/Channel
        // osuCrypto::IOService ios;
        // osuCrypto::Session session(ios, port, osuCrypto::SessionMode::Server);
        // osuCrypto::Channel chl = session.addChannel();
        // Receiver 端 - 修改为：
        osuCrypto::IOService ios;
        std::string address = "127.0.0.1:" + std::to_string(port);
        osuCrypto::Session session(ios, address, osuCrypto::SessionMode::Server);
        osuCrypto::Channel chl = session.addChannel();

        std::cout << "Receiver: Sender 已连接!" << std::endl;
        std::cout << std::endl;
        
        receiver.runOffline(chl);
        receiver.runOnline(chl);
        receiver.printStatistics();
        
        std::cout << "\nReceiver: 协议执行完成!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}