# FuzzyPSI-hamming
Implementation of Hamming-based Fuzzy Private Set Intersection

# Fuzzy Private Set Intersection (FPSI) for Hamming Distance

Optimized implementation of FPSI protocol using FHE (Fully Homomorphic Encryption) and E-LSH (Locality-Sensitive Hashing) for secure computation on binary vectors.

## Overview

This implementation provides two FPSI protocols:
- **FHE-based FPSI** (`Π^FHE_FPSI`): Using SEAL library for homomorphic encryption
- **OKVS-based FPSI** (`Π^OKVS_FPSI`): Using Oblivious Key-Value Store (planned)

### Key Features

- **Hamming Distance Matching**: Find vectors within distance threshold δ
- **Per-Vector Batching**: Pack each d-bit vector into one ciphertext (128× communication reduction)
- **Batch Transmission**: Send encrypted data in batches to avoid network timeout
- **E-LSH Fmap**: Efficient locality-sensitive hashing for fuzzy matching

## Performance

### Communication Optimization

| Metric | Original | Optimized | Improvement |
|--------|----------|-----------|-------------|
| Offline Ciphertexts | n×d = 32,768 | n = 256 | **128×** |
| Offline Data | ~512 MB | ~4 MB | **128×** |
| Online per Query | d = 128 | d = 128 | Same |

### Parameters

```
n, m = 256          # Dataset sizes
d = 128             # Vector dimension  
δ = 10              # Hamming distance threshold
L = 8               # Number of LSH hash functions
```

## Prerequisites

### Required Libraries

1. **Microsoft SEAL** (v4.0+)
   ```bash
   git clone https://github.com/microsoft/SEAL.git
   cd SEAL
   cmake -S . -B build -DSEAL_BUILD_EXAMPLES=OFF
   cmake --build build
   sudo cmake --install build
   ```

2. **libOTe** (cryptoTools)
   ```bash
   git clone https://github.com/osu-crypto/libOTe.git
   cd libOTe
   python build.py --setup --all
   ```

3. **Additional Dependencies**
   ```bash
   sudo apt-get install build-essential cmake git
   ```

## Project Structure

```
FPSI-hamming/
├── fpsi_receiver_fixed.cpp    # Receiver implementation
├── fpsi_sender_fixed.cpp      # Sender implementation
├── elsh.h                      # E-LSH Fmap implementation
├── band_okvs.h                 # OKVS encoding/decoding
├── utils.h                     # Utility functions
├── secure_primitives.h         # Crypto primitives (PEQT, OT, etc.)
├── CMakeLists.txt              # Build configuration
└── README.md                   # This file
```

## Building

### Option 1: Using CMake

```bash
mkdir build
cd build
cmake ..
make
```

### Option 2: Manual Compilation

```bash
g++ -std=c++17 fpsi_receiver_fixed.cpp -o receiver_fixed \
    -I/usr/local/include/SEAL-4.0 \
    -I/path/to/libOTe \
    -L/usr/local/lib \
    -lseal -lcryptoTools -lpthread -O3

g++ -std=c++17 fpsi_sender_fixed.cpp -o sender_fixed \
    -I/usr/local/include/SEAL-4.0 \
    -I/path/to/libOTe \
    -L/usr/local/lib \
    -lseal -lcryptoTools -lpthread -O3
```

## Running the Protocol

### Step 1: Start Receiver (Server)

```bash
./receiver_fixed [port]

# Example:
./receiver_fixed 12345
```

Expected output:
```
========================================
FPSI 协议 - Receiver (优化版)
========================================
参数: n=256, d=128, δ=10, L=8
监听端口: 12345
========================================

Receiver: 等待连接...
```

### Step 2: Start Sender (Client)

In a separate terminal:

```bash
./sender_fixed [ip] [port]

# Example:
./sender_fixed 127.0.0.1 12345
```

### Expected Workflow

1. **Offline Phase** (~30-60 seconds)
   - E-LSH ID generation
   - OKVS construction and transmission
   - Encrypted vectors transmission (batched)
   - Public key exchange

2. **Online Phase** (~2-5 minutes for m=256)
   - Query processing with homomorphic operations
   - Secret-shared equality tests
   - Oblivious transfer of results

3. **Output**
   - Receiver: Fuzzy intersection set
   - Statistics: Communication cost and running time

## Configuration

### Adjusting Parameters

Edit the `main()` function in both files:

```cpp
int n = 256;      // Receiver dataset size
int m = 256;      // Sender dataset size  
int d = 128;      // Vector dimension
int delta = 10;   // Hamming distance threshold
int L = 8;        // LSH hash functions (4-32)
```

### Batch Size

Modify `BATCH_SIZE` in `sendEncryptedVectorsBatched()`:

```cpp
const int BATCH_SIZE = 16;  // Vectors per batch (8-32)
```

Smaller batch size: More stable, slower
Larger batch size: Faster, may cause network issues

### Testing with Smaller Scale

For quick testing:

```cpp
int n = 16;       // Start small
int m = 16;
int L = 4;        // Fewer hash functions
```

## Protocol Details

### Offline Phase

**Receiver:**
1. Generate E-LSH IDs for dataset W
2. Construct OKVS mapping ID → vector index
3. Pack each vector into one ciphertext (d bits → 1 cipher)
4. Send in batches of 16 with synchronization
5. Transmit public key

**Sender:**
1. Generate E-LSH IDs for dataset Q
2. Receive OKVS encoding
3. Receive packed ciphertexts in batches
4. Receive public key

### Online Phase

For each query vector q_j:

1. **LSH Matching**: Find candidate IDs using E-LSH
2. **OKVS Decode**: Retrieve encrypted receiver vector
3. **Masked Comparison**: 
   - Generate random mask
   - Compute homomorphic sum
4. **Distance Computation**: Use secret-shared PEQT
5. **Threshold Check**: Compare Hamming distance ≤ δ
6. **OT Transfer**: Send result if match found

## Troubleshooting

### "End of file" Network Error

**Causes:**
- Sending too much data at once
- Network timeout
- Memory issues

**Solutions:**
1. Reduce batch size (16 → 8)
2. Reduce L parameter (8 → 4)
3. Test with smaller n/m first (256 → 64)

### Segmentation Fault

**Common causes:**
- Invalid OKVS decode (vector index out of range)
- FHE parameter mismatch
- Insufficient memory

**Debug:**
```cpp
// Add bounds checking
if (vec_index >= packed_vectors_.size()) {
    std::cerr << "Invalid index: " << vec_index << std::endl;
    return createDummyCipherVector();
}
```

### Slow Performance

**Optimizations:**
1. Use release build: `-O3` flag
2. Enable parallel SEAL operations
3. Reduce poly_modulus_degree (8192 → 4096) for testing
4. Decrease L (8 → 4)

## Experimental Results

### Expected Performance (n=m=256, d=128, δ=10, L=8)

| Phase | Communication | Time |
|-------|---------------|------|
| Offline | ~4 MB | 30-60s |
| Online | ~15 MB | 2-5 min |
| **Total** | **~19 MB** | **2.5-6 min** |

### Comparison with Baseline

vs. [AC:GQLLW24] (n=m=256):
- Communication: ~90 MB → ~19 MB (**4.7× improvement**)
- Time: ~4s → ~3 min (offline heavier, online lighter)

## Known Limitations

1. **Parameter L affects accuracy**: 
   - L=4: Fast but lower recall
   - L=32: High recall but slow
   - L=8: Balanced (recommended)

2. **Large datasets**: 
   - n > 1024 requires more optimization
   - Consider using OKVS-based version for n > 4096

3. **False positives/negatives**:
   - E-LSH is probabilistic
   - Tune L based on required accuracy

## Citation

If you use this code, please cite:

```bibtex
@inproceedings{your-fpsi-2024,
  title={Efficient Fuzzy Private Set Intersection for Hamming Distance},
  author={Your Name},
  booktitle={Conference},
  year={2024}
}
```

## References

- [AC:GQLLW24] - Baseline FPSI protocol
- Microsoft SEAL: https://github.com/microsoft/SEAL
- libOTe: https://github.com/osu-crypto/libOTe

## License

MIT License (or your preferred license)

## Contact

For questions or issues, please open an issue on GitHub or contact [your-email].

---

**Last Updated**: 2025-01-04
**Version**: 1.0 (Optimized with Per-Vector Batching)
