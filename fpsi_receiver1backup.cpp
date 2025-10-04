#include <iostream>
#include <vector>
#include <set>
#include <map>
#include <memory>
#include <sstream>
#include <algorithm>

#include <seal/seal.h>
#include "cryptoTools/Common/Defines.h"
#include "cryptoTools/Common/block.h"
#include "cryptoTools/Crypto/PRNG.h"
#include "cryptoTools/Network/Channel.h"
#include "cryptoTools/Network/Session.h"
#include "cryptoTools/Network/IOService.h"

#include "band_okvs.h"
#include "elsh.h"
#include "utils.h"
#include "secure_primitives.h"

using namespace osuCrypto;
using namespace seal;
using namespace band_okvs;

class FPSIReceiverFixed {
public:
    FPSIReceiverFixed(int n, int d, int delta, int L)
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
        encoder_ = std::make_unique<BatchEncoder>(*context_);
        
        slot_count_ = encoder_->slot_count();
        
        std::cout << "Receiver: SEAL初始化完成" << std::endl;
        std::cout << "  Slot count: " << slot_count_ << std::endl;
    }
    
    void generateData() {
        std::cout << "Receiver: 生成 " << n_ << " 个 " << d_ << " 维向量..." << std::endl;
        
        W_.resize(n_);
        for (int i = 0; i < n_; ++i) {
            W_[i] = utils::generateRandomBinaryVector(d_, prng_);
        }
        
        std::cout << "Receiver: 数据生成完成" << std::endl;
    }
    
    void runOffline(Channel& chl) {
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
        
        buildAndSendOKVS(chl);
        sendEncryptedVectorsBatched(chl);
        sendPublicKey(chl);
        
        timer.stop();
        offline_time_ = timer.getElapsedSeconds();
        
        std::cout << "Receiver: 离线阶段完成 - " << offline_time_ << " 秒" << std::endl;
        offline_comm_.print("离线");
    }
    
    void buildAndSendOKVS(Channel& chl) {
        std::cout << "Receiver: 构造 OKVS..." << std::endl;
        
        std::vector<block> okvs_keys;
        std::vector<block> okvs_values;
        
        for (int i = 0; i < n_; ++i) {
            for (const auto& id_str : ID_W_[i]) {
                std::hash<std::string> hasher;
                uint64_t hash_val = hasher(id_str);
                block key(hash_val, i);
                
                // 值：只存储向量索引
                block value(i, 0);
                
                okvs_keys.push_back(key);
                okvs_values.push_back(value);
            }
        }
        
        std::cout << "Receiver: OKVS 输入大小 = " << okvs_keys.size() << std::endl;
        
        double epsilon = 0.05;
        int m_okvs = static_cast<int>((1 + epsilon) * okvs_keys.size());
        int band_length = okvsBandLength(okvs_keys.size());
        
        okvs_seed_ = block(prng_.get<uint64_t>(), prng_.get<uint64_t>());
        
        BandOkvs okvs;
        okvs.Init(okvs_keys.size(), m_okvs, band_length, okvs_seed_);
        
        okvs_encoded_.resize(okvs.Size());
        
        if (!okvs.Encode(okvs_keys.data(), okvs_values.data(), okvs_encoded_.data())) {
            throw std::runtime_error("OKVS encoding failed");
        }
        
        uint64_t okvs_size = okvs_encoded_.size();
        chl.send(okvs_size);
        chl.send(okvs_encoded_.data(), okvs_size);
        chl.send(okvs_seed_);
        chl.send(m_okvs);
        chl.send(band_length);
        chl.send((int)okvs_keys.size());
        
        offline_comm_.addSent(sizeof(uint64_t) + okvs_size * sizeof(block) + 
                             sizeof(block) + sizeof(int) * 3);
        
        std::cout << "Receiver: OKVS 发送完成 (" 
                  << okvs_size * sizeof(block) / (1024.0 * 1024.0) << " MB)" << std::endl;
    }
    
    void sendEncryptedVectorsBatched(Channel& chl) {
        std::cout << "Receiver: 分批发送加密向量..." << std::endl;
        std::cout << "Receiver: 将 " << n_ << " 个向量各自打包到一个密文" << std::endl;
        std::cout << "Receiver: 通信量从 " << (n_ * d_) << " 个密文减少到 " 
                  << n_ << " 个密文 (压缩 " << d_ << "×)" << std::endl;
        
        chl.send(n_);
        offline_comm_.addSent(sizeof(int));
        
        const int BATCH_SIZE = 16;
        int num_batches = (n_ + BATCH_SIZE - 1) / BATCH_SIZE;
        
        for (int batch = 0; batch < num_batches; ++batch) {
            int batch_start = batch * BATCH_SIZE;
            int batch_end = std::min(batch_start + BATCH_SIZE, n_);
            
            std::cout << "Receiver: 发送批次 " << (batch + 1) << "/" << num_batches 
                      << " (向量 " << batch_start << "-" << (batch_end - 1) << ")" << std::endl;
            
            for (int i = batch_start; i < batch_end; ++i) {
                std::vector<uint64_t> packed_vec;
                for (int k = 0; k < d_; ++k) {
                    packed_vec.push_back(W_[i][k]);
                }
                
                // 填充到 slot_count
                while (packed_vec.size() < slot_count_) {
                    packed_vec.push_back(0);
                }
                
                Plaintext plain;
                encoder_->encode(packed_vec, plain);
                
                Ciphertext cipher;
                encryptor_->encrypt(plain, cipher);
                
                sendCiphertext(cipher, chl);
            }
            
            // 批次同步
            std::string sync_msg = "BATCH_" + std::to_string(batch);
            chl.send(sync_msg);
            
            std::string ack;
            chl.recv(ack);
            
            if (ack != "ACK") {
                throw std::runtime_error("Batch sync failed");
            }
        }
        
        std::cout << "Receiver: 所有加密向量发送完成" << std::endl;
    }
    
    void sendPublicKey(Channel& chl) {
        std::stringstream pk_stream;
        public_key_.save(pk_stream);
        std::string pk_str = pk_stream.str();
        chl.send(pk_str);
        offline_comm_.addSent(pk_str.size());
        
        std::cout << "Receiver: 公钥发送完成 (" 
                  << pk_str.size() / (1024.0 * 1024.0) << " MB)" << std::endl;
    }
    
    void runOnline(Channel& chl) {
        std::cout << "\n========== Receiver: 在线阶段 ==========" << std::endl;
        
        Timer timer;
        timer.start();
        
        int m_sender;
        chl.recv(m_sender);
        online_comm_.addReceived(sizeof(int));
        
        std::cout << "Receiver: Sender 数据集大小 = " << m_sender << std::endl;
        
        matched_sender_indices_.clear();
        fuzzy_intersection_.clear();
        
        for (int j = 0; j < m_sender; ++j) {
            if (j % 100 == 0 && j > 0) {
                std::cout << "Receiver: 进度 " << j << "/" << m_sender << std::endl;
            }
            
            bool found_match = processQuery(j, chl);
            
            if (found_match) {
                matched_sender_indices_.insert(j);
            }
        }
        
        timer.stop();
        online_time_ = timer.getElapsedSeconds();
        
        std::cout << "Receiver: 找到 " << matched_sender_indices_.size() 
                  << " 个匹配" << std::endl;
        std::cout << "Receiver: 在线阶段完成 - " << online_time_ << " 秒" << std::endl;
        online_comm_.print("在线");
    }
    
    bool processQuery(int j, Channel& chl) {
        int rate_s = L_;
        std::vector<uint8_t> e_flags;
        
        for (int ell = 0; ell < rate_s; ++ell) {
            std::vector<Ciphertext> cipher(d_);
            for (int k = 0; k < d_; ++k) {
                receiveCiphertext(cipher[k], chl);
            }
            
            std::vector<uint8_t> u(d_);
            chl.recv(u.data(), d_);
            online_comm_.addReceived(d_);
            
            std::vector<uint8_t> v = decryptVector(cipher);
            
            int num_slots = (d_ + 7) / 8;
            std::vector<uint8_t> shares_a(num_slots);
            std::vector<uint8_t> shares_b(num_slots);
            
            for (int slot = 0; slot < num_slots; ++slot) {
                uint8_t all_equal = 1;
                for (int bit = 0; bit < 8 && slot * 8 + bit < d_; ++bit) {
                    int idx = slot * 8 + bit;
                    if (u[idx] != v[idx]) {
                        all_equal = 0;
                        break;
                    }
                }
                
                SecretSharedPEQT::generateShares(
                    all_equal, all_equal, shares_a[slot], shares_b[slot], prng_
                );
            }
            
            std::vector<Ciphertext> enc_shares_a(num_slots);
            for (int i = 0; i < num_slots; ++i) {
                Plaintext plain;
                encoder_->encode(std::vector<uint64_t>{shares_a[i]}, plain);
                encryptor_->encrypt(plain, enc_shares_a[i]);
                sendCiphertext(enc_shares_a[i], chl);
            }
            
            Ciphertext masked_sum;
            receiveCiphertext(masked_sum, chl);
            
            uint64_t random_mask;
            chl.recv(random_mask);
            online_comm_.addReceived(sizeof(uint64_t));
            
            Plaintext plain_result;
            decryptor_->decrypt(masked_sum, plain_result);
            
            std::vector<uint64_t> decoded;
            encoder_->decode(plain_result, decoded);
            
            int64_t sum_diff = static_cast<int64_t>(decoded[0]) - 
                              static_cast<int64_t>(random_mask);
            
            int match_count = num_slots - std::abs(sum_diff);
            int threshold_slots = num_slots - (delta_ / 8) - 1;
            uint8_t e_j_ell = (match_count >= threshold_slots) ? 1 : 0;
            
            chl.send(e_j_ell);
            online_comm_.addSent(sizeof(uint8_t));
            
            e_flags.push_back(e_j_ell);
        }
        
        bool has_match = PrivateEqualityTest::testAnyOne(e_flags, chl, prng_, false);
        
        std::vector<uint8_t> received_vector = 
            ObliviousTransfer::receive<std::vector<uint8_t>>(
                has_match ? 1 : 0, chl
            );
        
        if (has_match) {
            fuzzy_intersection_.push_back(received_vector);
        }
        
        return has_match;
    }
    
    std::vector<uint8_t> decryptVector(const std::vector<Ciphertext>& enc_vec) {
        std::vector<uint8_t> result(enc_vec.size());
        
        for (size_t k = 0; k < enc_vec.size(); ++k) {
            Plaintext plain;
            decryptor_->decrypt(enc_vec[k], plain);
            
            std::vector<uint64_t> decoded;
            encoder_->decode(plain, decoded);
            
            result[k] = static_cast<uint8_t>(decoded[0] & 1);
        }
        
        return result;
    }
    
    void sendCiphertext(const Ciphertext& cipher, Channel& chl) {
        std::stringstream ss;
        cipher.save(ss);
        std::string str = ss.str();
        
        uint64_t size = str.size();
        chl.send(size);
        chl.send(str.data(), size);
        offline_comm_.addSent(sizeof(uint64_t) + size);
    }
    
    void receiveCiphertext(Ciphertext& cipher, Channel& chl) {
        uint64_t size;
        chl.recv(size);
        
        std::string str(size, '\0');
        chl.recv(str.data(), size);
        online_comm_.addReceived(sizeof(uint64_t) + size);
        
        std::stringstream ss(str);
        cipher.load(*context_, ss);
    }
    
    int okvsBandLength(int n) {
        if (n <= (1 << 14)) return 339;
        else if (n <= (1 << 16)) return 350;
        else if (n <= (1 << 18)) return 366;
        else if (n <= (1 << 20)) return 377;
        else if (n <= (1 << 22)) return 396;
        else if (n <= (1 << 24)) return 413;
        else throw std::runtime_error("No valid band length for OKVS");
    }
    
    void printStatistics() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Receiver 协议统计" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "参数: n=" << n_ << ", d=" << d_ 
                  << ", δ=" << delta_ << ", L=" << L_ << std::endl;
        std::cout << "模糊交集大小: " << fuzzy_intersection_.size() << std::endl;
        std::cout << std::endl;
        
        std::cout << "离线阶段: " << offline_time_ << " 秒" << std::endl;
        std::cout << "  通信: " << offline_comm_.getTotalMegabytes() << " MB" << std::endl;
        std::cout << std::endl;
        
        std::cout << "在线阶段: " << online_time_ << " 秒" << std::endl;
        std::cout << "  通信: " << online_comm_.getTotalMegabytes() << " MB" << std::endl;
        std::cout << std::endl;
        
        std::cout << "总计: " << (offline_time_ + online_time_) << " 秒" << std::endl;
        std::cout << "  通信: " << (offline_comm_.getTotalMegabytes() + 
                     online_comm_.getTotalMegabytes()) << " MB" << std::endl;
        std::cout << "========================================" << std::endl;
    }

private:
    int n_, d_, delta_, L_;
    size_t slot_count_;
    
    PRNG prng_;
    std::unique_ptr<ELSHFmap> elsh_;
    
    std::shared_ptr<SEALContext> context_;
    SecretKey secret_key_;
    PublicKey public_key_;
    std::unique_ptr<Encryptor> encryptor_;
    std::unique_ptr<Decryptor> decryptor_;
    std::unique_ptr<Evaluator> evaluator_;
    std::unique_ptr<BatchEncoder> encoder_;
    
    std::vector<std::vector<uint8_t>> W_;
    std::vector<std::set<std::string>> ID_W_;
    
    std::vector<block> okvs_encoded_;
    block okvs_seed_;
    
    std::set<int> matched_sender_indices_;
    std::vector<std::vector<uint8_t>> fuzzy_intersection_;
    
    double offline_time_ = 0.0;
    double online_time_ = 0.0;
    CommStats offline_comm_;
    CommStats online_comm_;
};

int main(int argc, char** argv) {
    int n = 256;
    int d = 128;
    int delta = 10;
    int L = 8;
    
    int port = 12345;
    if (argc > 1) port = std::atoi(argv[1]);
    
    std::cout << "========================================" << std::endl;
    std::cout << "FPSI 协议 - Receiver (修复版)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "参数: n=" << n << ", d=" << d << ", δ=" << delta << ", L=" << L << std::endl;
    std::cout << "监听端口: " << port << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        FPSIReceiverFixed receiver(n, d, delta, L);
        receiver.generateData();
        
        std::cout << "\nReceiver: 等待连接..." << std::endl;
        
        IOService ios;
        std::string address = "127.0.0.1:" + std::to_string(port);
        Session session(ios, address, SessionMode::Server);
        Channel chl = session.addChannel();
        
        std::cout << "Receiver: 已连接!" << std::endl;
        
        receiver.runOffline(chl);
        receiver.runOnline(chl);
        receiver.printStatistics();
        
        std::cout << "\n✓ Receiver: 协议执行完成!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}