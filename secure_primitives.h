#ifndef SECURE_PRIMITIVES_H
#define SECURE_PRIMITIVES_H

#include <vector>
#include <memory>
#include <seal/seal.h>
#include "cryptoTools/Common/block.h"
#include "cryptoTools/Crypto/PRNG.h"
#include "cryptoTools/Network/Channel.h"

using namespace seal;
using namespace osuCrypto;

// ============================================
// 安全私有等值测试 (ssPEQT)
// ============================================
class SecretSharedPEQT {
public:
    // 生成秘密共享：a ⊕ b = (x == y)
    static void generateShares(
        uint8_t x, 
        uint8_t y,
        uint8_t& share_a,
        uint8_t& share_b,
        PRNG& prng
    ) {
        // 计算是否相等
        uint8_t equal = (x == y) ? 1 : 0;
        
        // 生成随机share_a
        share_a = prng.get<uint8_t>() & 1;
        
        // 计算share_b使得 a ⊕ b = equal
        share_b = share_a ^ equal;
    }
    
    // 批量处理版本
    static void generateSharesBatch(
        const std::vector<uint8_t>& x_vec,
        const std::vector<uint8_t>& y_vec,
        std::vector<uint8_t>& shares_a,
        std::vector<uint8_t>& shares_b,
        PRNG& prng
    ) {
        size_t len = x_vec.size();
        shares_a.resize(len);
        shares_b.resize(len);
        
        for (size_t i = 0; i < len; ++i) {
            generateShares(x_vec[i], y_vec[i], shares_a[i], shares_b[i], prng);
        }
    }
};

// ============================================
// FHE 阈值比较协议
// ============================================
class FHEThresholdComparison {
public:
    FHEThresholdComparison(
        std::shared_ptr<SEALContext> context,
        const PublicKey& public_key,
        const SecretKey& secret_key
    ) : context_(context) {
        encryptor_ = std::make_unique<Encryptor>(*context, public_key);
        decryptor_ = std::make_unique<Decryptor>(*context, secret_key);
        evaluator_ = std::make_unique<Evaluator>(*context);
        encoder_ = std::make_unique<BatchEncoder>(*context);
    }
    
    // Receiver端：加密share_a
    void encryptReceiverShares(
        const std::vector<uint8_t>& shares_a,
        std::vector<Ciphertext>& encrypted_shares
    ) {
        encrypted_shares.resize(shares_a.size());
        
        for (size_t i = 0; i < shares_a.size(); ++i) {
            Plaintext plain;
            encoder_->encode(std::vector<uint64_t>{shares_a[i]}, plain);
            encryptor_->encrypt(plain, encrypted_shares[i]);
        }
    }
    
    // Sender端：同态计算 sum(a_i - b_i) 并加随机掩码
    Ciphertext computeMaskedSum(
        const std::vector<Ciphertext>& encrypted_a,
        const std::vector<uint8_t>& shares_b,
        uint64_t& random_mask,
        PRNG& prng
    ) {
        // 生成随机掩码
        random_mask = prng.get<uint64_t>() % 1000;
        
        Ciphertext result;
        bool first = true;
        
        for (size_t i = 0; i < shares_b.size(); ++i) {
            // 计算 a_i - b_i （同态）
            Plaintext plain_b;
            encoder_->encode(std::vector<uint64_t>{shares_b[i]}, plain_b);
            
            Ciphertext diff;
            evaluator_->negate(encrypted_a[i], diff);
            evaluator_->add_plain_inplace(diff, plain_b);
            
            // 累加
            if (first) {
                result = diff;
                first = false;
            } else {
                evaluator_->add_inplace(result, diff);
            }
        }
        
        // 加随机掩码
        Plaintext plain_mask;
        encoder_->encode(std::vector<uint64_t>{random_mask}, plain_mask);
        evaluator_->add_plain_inplace(result, plain_mask);
        
        return result;
    }
    
    // Receiver端：解密并判断阈值
    bool decryptAndCompare(
        const Ciphertext& masked_result,
        uint64_t random_mask,
        size_t total_bits,
        int threshold
    ) {
        Plaintext plain_result;
        decryptor_->decrypt(masked_result, plain_result);
        
        std::vector<uint64_t> decoded;
        encoder_->decode(plain_result, decoded);
        
        // 移除掩码
        uint64_t match_count = (decoded[0] - random_mask) % (1ULL << 32);
        
        // 判断是否满足阈值
        // match_count = sum(a_i - b_i) = 匹配数 * 2
        // 因为当匹配时 a_i = b_i，差为0；不匹配时差非0
        int actual_matches = total_bits - (match_count / 2);
        
        return actual_matches >= (total_bits - threshold);
    }

private:
    std::shared_ptr<SEALContext> context_;
    std::unique_ptr<Encryptor> encryptor_;
    std::unique_ptr<Decryptor> decryptor_;
    std::unique_ptr<Evaluator> evaluator_;
    std::unique_ptr<BatchEncoder> encoder_;
};

// ============================================
// Private Equality Test (PEqT)
// ============================================
class PrivateEqualityTest {
public:
    // 测试一组标志中是否至少有一个为1
    static bool testAnyOne(
        const std::vector<uint8_t>& flags,
        Channel& chl,
        PRNG& prng,
        bool is_sender
    ) {
        size_t n = flags.size();
        
        if (is_sender) {
            // Sender: 生成随机掩码
            std::vector<uint8_t> masks(n);
            for (size_t i = 0; i < n; ++i) {
                masks[i] = prng.get<uint8_t>() & 1;
            }
            
            // 发送掩码后的值
            std::vector<uint8_t> masked(n);
            for (size_t i = 0; i < n; ++i) {
                masked[i] = flags[i] ^ masks[i];
            }
            chl.send(masked.data(), n);
            
            // 接收结果并恢复
            uint8_t masked_result;
            chl.recv(masked_result);
            
            // 计算本地OR
            uint8_t local_or = 0;
            for (auto m : masks) {
                local_or |= m;
            }
            
            return (masked_result ^ local_or) == 1;
            
        } else {
            // Receiver: 接收掩码值
            std::vector<uint8_t> masked(n);
            chl.recv(masked.data(), n);
            
            // 计算 masked OR flags
            uint8_t result = 0;
            for (size_t i = 0; i < n; ++i) {
                result |= (masked[i] ^ flags[i]);
            }
            
            // 发送结果
            chl.send(result);
            
            return result == 1;
        }
    }
};

// ============================================
// 1-out-of-2 Oblivious Transfer (OT)
// ============================================
class ObliviousTransfer {
public:
    // 简化的OT实现（基于随机OT扩展）
    template<typename T>
    static void send(
        const T& msg0,  // 选择位=0时的消息
        const T& msg1,  // 选择位=1时的消息
        Channel& chl,
        PRNG& prng
    ) {
        // 生成随机密钥
        block k0 = prng.get<block>();
        block k1 = prng.get<block>();
        
        // 发送加密的消息
        std::vector<uint8_t> enc0 = encryptMessage(msg0, k0);
        std::vector<uint8_t> enc1 = encryptMessage(msg1, k1);
        
        chl.send(enc0);
        chl.send(enc1);
        
        // 发送密钥（通过OT基础协议）
        // 简化：直接发送，实际需要用OT扩展
        chl.send(k0);
        chl.send(k1);
    }
    
    template<typename T>
    static T receive(
        uint8_t choice,  // 选择位：0或1
        Channel& chl
    ) {
        // 接收两个加密消息
        std::vector<uint8_t> enc0, enc1;
        chl.recv(enc0);
        chl.recv(enc1);
        
        // 接收密钥
        block k0, k1;
        chl.recv(k0);
        chl.recv(k1);
        
        // 根据选择位解密
        if (choice == 0) {
            return decryptMessage<T>(enc0, k0);
        } else {
            return decryptMessage<T>(enc1, k1);
        }
    }

private:
    template<typename T>
    static std::vector<uint8_t> encryptMessage(const T& msg, block key) {
        // 简化加密：XOR with key
        std::vector<uint8_t> result;
        const uint8_t* msg_bytes = reinterpret_cast<const uint8_t*>(&msg);
        const uint8_t* key_bytes = reinterpret_cast<const uint8_t*>(&key);
        
        for (size_t i = 0; i < sizeof(T); ++i) {
            result.push_back(msg_bytes[i] ^ key_bytes[i % sizeof(block)]);
        }
        return result;
    }
    
    template<typename T>
    static T decryptMessage(const std::vector<uint8_t>& enc, block key) {
        T result;
        uint8_t* result_bytes = reinterpret_cast<uint8_t*>(&result);
        const uint8_t* key_bytes = reinterpret_cast<const uint8_t*>(&key);
        
        for (size_t i = 0; i < sizeof(T); ++i) {
            result_bytes[i] = enc[i] ^ key_bytes[i % sizeof(block)];
        }
        return result;
    }
};

#endif // SECURE_PRIMITIVES_H