#include <iostream>
#include <vector>
#include <set>
#include <map>
#include <memory>
#include <sstream>

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

class FPSISenderFixed {
public:
    FPSISenderFixed(int m, int d, int delta, int L)
        : m_(m), d_(d), delta_(delta), L_(L) {
        
        prng_.SetSeed(block(123456, 789012));
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
        evaluator_ = std::make_unique<Evaluator>(*context_);
        encoder_ = std::make_unique<BatchEncoder>(*context_);
        
        slot_count_ = encoder_->slot_count();
        
        std::cout << "Sender: SEAL 参数初始化完成" << std::endl;
        std::cout << "  Slot count: " << slot_count_ << std::endl;
    }
    
    void generateData() {
        std::cout << "Sender: 生成 " << m_ << " 个 " << d_ << " 维向量..." << std::endl;
        
        Q_.resize(m_);
        for (int i = 0; i < m_; ++i) {
            Q_[i] = utils::generateRandomBinaryVector(d_, prng_);
        }
        
        std::cout << "Sender: 数据生成完成" << std::endl;
    }
    
    void runOffline(Channel& chl) {
        std::cout << "\n========== Sender: 离线阶段开始 ==========" << std::endl;
        
        Timer timer;
        timer.start();
        
        std::cout << "Sender: 计算 E-LSH ID..." << std::endl;
        ID_Q_ = elsh_->computeIDBatch(Q_);
        
        uint64_t id_count = 0;
        for (const auto& ids : ID_Q_) {
            id_count += ids.size();
        }
        std::cout << "Sender: 生成了 " << id_count << " 个 ID" << std::endl;
        
        receiveOKVS(chl);
        receiveEncryptedVectorsBatched(chl);
        receivePublicKey(chl);
        
        timer.stop();
        offline_time_ = timer.getElapsedSeconds();
        
        std::cout << "Sender: 离线阶段完成 - " << offline_time_ << " 秒" << std::endl;
        offline_comm_.print("离线");
    }
    
    void receiveOKVS(Channel& chl) {
        std::cout << "Sender: 接收 OKVS..." << std::endl;
        
        uint64_t okvs_size;
        chl.recv(okvs_size);
        
        okvs_encoded_.resize(okvs_size);
        chl.recv(okvs_encoded_.data(), okvs_size);
        offline_comm_.addReceived(sizeof(uint64_t) + okvs_size * sizeof(block));
        
        chl.recv(okvs_seed_);
        chl.recv(okvs_m_);
        chl.recv(okvs_band_length_);
        chl.recv(okvs_n_items_);
        offline_comm_.addReceived(sizeof(block) + sizeof(int) * 3);
        
        std::cout << "Sender: OKVS 参数 - size=" << okvs_size 
                  << ", n_items=" << okvs_n_items_ << std::endl;
        
        okvs_decoder_ = std::make_unique<BandOkvs>();
        okvs_decoder_->Init(okvs_n_items_, okvs_m_, okvs_band_length_, okvs_seed_);
    }
    
    void receiveEncryptedVectorsBatched(Channel& chl) {
        std::cout << "Sender: 分批接收加密向量..." << std::endl;
        
        int n_receiver;
        chl.recv(n_receiver);
        offline_comm_.addReceived(sizeof(int));
        
        n_receiver_ = n_receiver;
        packed_vectors_.resize(n_receiver);
        
        std::cout << "Sender: Receiver向量数: " << n_receiver << std::endl;
        
        const int BATCH_SIZE = 16;
        int num_batches = (n_receiver + BATCH_SIZE - 1) / BATCH_SIZE;
        
        for (int batch = 0; batch < num_batches; ++batch) {
            int batch_start = batch * BATCH_SIZE;
            int batch_end = std::min(batch_start + BATCH_SIZE, n_receiver);
            
            std::cout << "Sender: 接收批次 " << (batch + 1) << "/" << num_batches << std::endl;
            
            for (int i = batch_start; i < batch_end; ++i) {
                receiveCiphertext(packed_vectors_[i], chl);
            }
            
            std::string sync_msg;
            chl.recv(sync_msg);
            chl.send(std::string("ACK"));
        }
        
        std::cout << "Sender: 接收了 " << n_receiver << " 个打包密文" << std::endl;
    }
    
    void receivePublicKey(Channel& chl) {
        std::cout << "Sender: 接收公钥..." << std::endl;
        
        std::string pk_str;
        chl.recv(pk_str);
        offline_comm_.addReceived(pk_str.size());
        
        std::stringstream pk_stream(pk_str);
        PublicKey public_key;
        public_key.load(*context_, pk_stream);
        
        encryptor_ = std::make_unique<Encryptor>(*context_, public_key);
        
        std::cout << "Sender: 公钥加载完成" << std::endl;
    }
    
    void runOnline(Channel& chl) {
        std::cout << "\n========== Sender: 在线阶段开始 ==========" << std::endl;
        
        Timer timer;
        timer.start();
        
        chl.send(m_);
        online_comm_.addSent(sizeof(int));
        
        std::cout << "Sender: 处理 " << m_ << " 个查询..." << std::endl;
        
        for (int j = 0; j < m_; ++j) {
            if (j % 100 == 0 && j > 0) {
                std::cout << "Sender: 进度 " << j << "/" << m_ << std::endl;
            }
            
            processQuery(j, chl);
        }
        
        timer.stop();
        online_time_ = timer.getElapsedSeconds();
        
        std::cout << "Sender: 在线阶段完成 - " << online_time_ << " 秒" << std::endl;
        online_comm_.print("在线");
    }
    
    void processQuery(int j, Channel& chl) {
        const auto& q_j = Q_[j];
        const auto& ids = ID_Q_[j];
        
        std::vector<uint8_t> e_flags;
        
        // 修复：确保不超过 L_ 和实际 ID 数量
        int max_iterations = std::min(static_cast<int>(ids.size()), L_);
        
        int ell = 0;
        for (const auto& id_str : ids) {
            if (ell >= max_iterations) break;
            
            std::vector<Ciphertext> enc_w = extractVectorFromPacked(id_str, j);
            
            std::vector<uint8_t> mask = utils::generateRandomBinaryVector(d_, prng_);
            
            std::vector<Ciphertext> cipher = computeHomomorphicSum(mask, enc_w);
            
            std::vector<uint8_t> u(d_);
            for (int k = 0; k < d_; ++k) {
                u[k] = mask[k] ^ q_j[k];
            }
            
            sendCiphertexts(cipher, chl);
            chl.send(u.data(), d_);
            online_comm_.addSent(d_);
            
            int num_slots = (d_ + 7) / 8;
            std::vector<Ciphertext> enc_shares_a(num_slots);
            
            for (int i = 0; i < num_slots; ++i) {
                receiveCiphertext(enc_shares_a[i], chl);
            }
            
            std::vector<uint8_t> shares_b = generateSenderShares(u, mask, num_slots);
            
            uint64_t random_mask = prng_.get<uint64_t>() % 1000;
            Ciphertext masked_sum = computeMaskedSumHomomorphic(
                enc_shares_a, shares_b, random_mask
            );
            
            sendCiphertext(masked_sum, chl);
            chl.send(random_mask);
            online_comm_.addSent(sizeof(uint64_t));
            
            uint8_t e_j_ell;
            chl.recv(e_j_ell);
            online_comm_.addReceived(sizeof(uint8_t));
            
            e_flags.push_back(e_j_ell);
            
            ell++;
        }
        
        bool has_match = PrivateEqualityTest::testAnyOne(e_flags, chl, prng_, true);
        
        std::vector<uint8_t> null_msg(d_, 0);
        ObliviousTransfer::send(null_msg, q_j, chl, prng_);
        
        if (has_match) {
            matched_queries_.insert(j);
        }
    }
    
    std::vector<Ciphertext> extractVectorFromPacked(const std::string& id_str, int query_idx) {
        std::hash<std::string> hasher;
        uint64_t hash_val = hasher(id_str);
        block okvs_key(hash_val, query_idx);
        
        block decoded_block(0, 0);  // 初始化为0
        const block* key_ptr = &okvs_key;
        
        try {
            // 尝试OKVS解码
            okvs_decoder_->Decode(key_ptr, okvs_encoded_.data(), &decoded_block);
            
            uint64_t vec_index = decoded_block.get<uint64_t>(0);
            
            // 关键修复：验证索引范围
            if (vec_index >= static_cast<uint64_t>(n_receiver_) || 
                vec_index >= packed_vectors_.size()) {
                // 索引超出范围，这个ID不在Receiver的数据集中
                return createDummyCipherVector();
            }
            
            // 索引有效，提取对应的向量
            return extractBitsFromPackedCipher(packed_vectors_[vec_index]);
            
        } catch (const std::exception& e) {
            // 捕获任何解码异常，返回虚拟密文
            return createDummyCipherVector();
        } catch (...) {
            // 捕获所有其他异常
            return createDummyCipherVector();
        }
    }
    
    std::vector<Ciphertext> extractBitsFromPackedCipher(const Ciphertext& packed) {
        std::vector<Ciphertext> result(d_);
        
        for (int k = 0; k < d_; ++k) {
            std::vector<uint64_t> mask(slot_count_, 0);
            mask[k] = 1;
            
            Plaintext mask_plain;
            encoder_->encode(mask, mask_plain);
            
            evaluator_->multiply_plain(packed, mask_plain, result[k]);
        }
        
        return result;
    }
    
    std::vector<Ciphertext> createDummyCipherVector() {
        std::vector<Ciphertext> result(d_);
        
        for (int k = 0; k < d_; ++k) {
            Plaintext plain;
            encoder_->encode(std::vector<uint64_t>{0}, plain);
            encryptor_->encrypt(plain, result[k]);
        }
        
        return result;
    }
    
    std::vector<Ciphertext> computeHomomorphicSum(
        const std::vector<uint8_t>& mask,
        const std::vector<Ciphertext>& enc_w
    ) {
        std::vector<Ciphertext> result(d_);
        
        for (int k = 0; k < d_; ++k) {
            Plaintext plain_mask;
            encoder_->encode(std::vector<uint64_t>{mask[k]}, plain_mask);
            
            Ciphertext enc_mask;
            encryptor_->encrypt(plain_mask, enc_mask);
            
            evaluator_->add(enc_mask, enc_w[k], result[k]);
        }
        
        return result;
    }
    
    std::vector<uint8_t> generateSenderShares(
        const std::vector<uint8_t>& u,
        const std::vector<uint8_t>& mask,
        int num_slots
    ) {
        std::vector<uint8_t> shares_b(num_slots);
        for (int slot = 0; slot < num_slots; ++slot) {
            shares_b[slot] = prng_.get<uint8_t>() & 1;
        }
        return shares_b;
    }
    
    Ciphertext computeMaskedSumHomomorphic(
        const std::vector<Ciphertext>& enc_shares_a,
        const std::vector<uint8_t>& shares_b,
        uint64_t random_mask
    ) {
        Ciphertext result;
        bool first = true;
        
        for (size_t i = 0; i < shares_b.size(); ++i) {
            Plaintext plain_b;
            encoder_->encode(std::vector<uint64_t>{shares_b[i]}, plain_b);
            
            Ciphertext diff = enc_shares_a[i];
            evaluator_->sub_plain_inplace(diff, plain_b);
            
            if (first) {
                result = diff;
                first = false;
            } else {
                evaluator_->add_inplace(result, diff);
            }
        }
        
        Plaintext plain_mask;
        encoder_->encode(std::vector<uint64_t>{random_mask}, plain_mask);
        evaluator_->add_plain_inplace(result, plain_mask);
        
        return result;
    }
    
    void sendCiphertexts(const std::vector<Ciphertext>& ciphers, Channel& chl) {
        for (const auto& cipher : ciphers) {
            sendCiphertext(cipher, chl);
        }
    }
    
    void sendCiphertext(const Ciphertext& cipher, Channel& chl) {
        std::stringstream ss;
        cipher.save(ss);
        std::string str = ss.str();
        
        uint64_t size = str.size();
        chl.send(size);
        chl.send(str.data(), size);
        online_comm_.addSent(sizeof(uint64_t) + size);
    }
    
    void receiveCiphertext(Ciphertext& cipher, Channel& chl) {
        uint64_t size;
        chl.recv(size);
        
        std::string str(size, '\0');
        chl.recv(str.data(), size);
        offline_comm_.addReceived(sizeof(uint64_t) + size);
        
        std::stringstream ss(str);
        cipher.load(*context_, ss);
    }
    
    void printStatistics() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Sender 协议统计" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "参数: m=" << m_ << ", d=" << d_ 
                  << ", δ=" << delta_ << ", L=" << L_ << std::endl;
        std::cout << "匹配查询数: " << matched_queries_.size() << std::endl;
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
    int m_, d_, delta_, L_;
    size_t slot_count_;
    
    PRNG prng_;
    std::unique_ptr<ELSHFmap> elsh_;
    
    std::shared_ptr<SEALContext> context_;
    std::unique_ptr<Encryptor> encryptor_;
    std::unique_ptr<Evaluator> evaluator_;
    std::unique_ptr<BatchEncoder> encoder_;
    
    std::vector<std::vector<uint8_t>> Q_;
    std::vector<std::set<std::string>> ID_Q_;
    
    std::vector<block> okvs_encoded_;
    block okvs_seed_;
    int okvs_m_;
    int okvs_band_length_;
    int okvs_n_items_;
    std::unique_ptr<BandOkvs> okvs_decoder_;
    
    std::vector<Ciphertext> packed_vectors_;
    int n_receiver_;
    
    std::set<int> matched_queries_;
    
    double offline_time_ = 0.0;
    double online_time_ = 0.0;
    CommStats offline_comm_;
    CommStats online_comm_;
};

int main(int argc, char** argv) {
    int m = 256;
    int d = 128;
    int delta = 10;
    int L = 8;
    
    std::string ip = "127.0.0.1";
    int port = 12345;
    
    if (argc > 1) ip = argv[1];
    if (argc > 2) port = std::atoi(argv[2]);
    
    std::cout << "========================================" << std::endl;
    std::cout << "FPSI 协议 - Sender (修复版)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "参数: m=" << m << ", d=" << d << ", δ=" << delta << ", L=" << L << std::endl;
    std::cout << "连接: " << ip << ":" << port << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        FPSISenderFixed sender(m, d, delta, L);
        sender.generateData();
        
        std::cout << "\nSender: 连接到 Receiver..." << std::endl;
        
        IOService ios;
        Session session(ios, ip, (u32)port, SessionMode::Client);
        Channel chl = session.addChannel();
        
        std::cout << "Sender: 连接成功!" << std::endl;
        
        sender.runOffline(chl);
        sender.runOnline(chl);
        sender.printStatistics();
        
        std::cout << "\n✓ Sender: 协议执行完成!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}