#!/bin/bash

# FPSI-FHE 性能测试脚本
# 测试参数: d=128, delta=4, L=32
# 测试数据集大小: 256, 512, 1024

set -e

# 颜色定义
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

print_header() {
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}$1${NC}"
    echo -e "${BLUE}========================================${NC}"
}

print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_test() {
    echo -e "${YELLOW}[TEST]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查可执行文件
if [ ! -f "build/fpsi_receiver" ] || [ ! -f "build/fpsi_sender" ]; then
    print_error "找不到可执行文件，请先编译项目"
    echo "运行: cd build && cmake .. && make -j"
    exit 1
fi

# 创建结果目录
RESULTS_DIR="fhe_benchmark_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RESULTS_DIR"

print_header "FPSI-FHE 性能测试"
echo "测试配置:"
echo "  维度 d = 128"
echo "  阈值 delta = 4"
echo "  哈希函数数量 L = 32"
echo "  数据集大小: 256, 512, 1024"
echo ""
echo "结果将保存到: $RESULTS_DIR"
echo ""

# 固定参数
PORT=12345
DIM=128
DELTA=4
HASH_L=32

# 测试的数据集大小
SET_SIZES=(256 512 1024)

# 备份原始文件
print_info "备份原始源文件..."
cp fpsi_receiver.cpp fpsi_receiver.cpp.original
cp fpsi_sender.cpp fpsi_sender.cpp.original

# 运行测试
test_count=0
total_tests=${#SET_SIZES[@]}

for size in "${SET_SIZES[@]}"; do
    test_count=$((test_count + 1))
    
    print_test "测试 $test_count/$total_tests"
    echo "  参数: n=m=$size, d=$DIM, δ=$DELTA, L=$HASH_L"
    
    # 生成测试文件名
    TEST_NAME="n${size}_d${DIM}_delta${DELTA}_L${HASH_L}"
    RECEIVER_LOG="$RESULTS_DIR/receiver_${TEST_NAME}.log"
    SENDER_LOG="$RESULTS_DIR/sender_${TEST_NAME}.log"
    
    # 修改源文件参数
    print_info "配置测试参数..."
    
    # 恢复备份（确保每次修改都基于原始文件）
    cp fpsi_receiver.cpp.original fpsi_receiver.cpp
    cp fpsi_sender.cpp.original fpsi_sender.cpp
    
    # 修改 receiver 参数
    sed -i "s/int n = [0-9]*;/int n = $size;/" fpsi_receiver.cpp
    sed -i "s/int d = [0-9]*;/int d = $DIM;/" fpsi_receiver.cpp
    sed -i "s/int delta = [0-9]*;/int delta = $DELTA;/" fpsi_receiver.cpp
    sed -i "s/int L = [0-9]*;/int L = $HASH_L;/" fpsi_receiver.cpp
    
    # 修改 sender 参数
    sed -i "s/int m = [0-9]*;/int m = $size;/" fpsi_sender.cpp
    sed -i "s/int d = [0-9]*;/int d = $DIM;/" fpsi_sender.cpp
    sed -i "s/int delta = [0-9]*;/int delta = $DELTA;/" fpsi_sender.cpp
    sed -i "s/int L = [0-9]*;/int L = $HASH_L;/" fpsi_sender.cpp
    
    # 重新编译
    print_info "重新编译..."
    cd build
    if ! make -j$(nproc) > /dev/null 2>&1; then
        print_error "编译失败"
        cd ..
        # 恢复原始文件
        mv fpsi_receiver.cpp.original fpsi_receiver.cpp
        mv fpsi_sender.cpp.original fpsi_sender.cpp
        exit 1
    fi
    cd ..
    
    # 启动 receiver
    print_info "启动 Receiver..."
    ./build/fpsi_receiver $PORT > "$RECEIVER_LOG" 2>&1 &
    RECEIVER_PID=$!
    
    # 等待 receiver 启动
    sleep 3
    
    # 检查 receiver 是否运行
    if ! ps -p $RECEIVER_PID > /dev/null; then
        print_error "Receiver 启动失败"
        echo "查看日志: cat $RECEIVER_LOG"
        cat "$RECEIVER_LOG"
        # 恢复原始文件
        mv fpsi_receiver.cpp.original fpsi_receiver.cpp
        mv fpsi_sender.cpp.original fpsi_sender.cpp
        exit 1
    fi
    
    # 启动 sender
    print_info "启动 Sender..."
    ./build/fpsi_sender 127.0.0.1 $PORT > "$SENDER_LOG" 2>&1
    SENDER_EXIT=$?
    
    # 等待 receiver 完成
    wait $RECEIVER_PID
    RECEIVER_EXIT=$?
    
    # 检查执行结果
    if [ $SENDER_EXIT -eq 0 ] && [ $RECEIVER_EXIT -eq 0 ]; then
        print_info "测试完成 ✓"
        
        # 提取并显示关键统计信息
        echo ""
        echo "--- 结果摘要 ---"
        
        # 从日志中提取时间和通信量
        offline_time=$(grep -A 1 "离线阶段:" "$RECEIVER_LOG" | grep "时间:" | awk '{print $2}' | head -1)
        online_time=$(grep -A 1 "在线阶段:" "$RECEIVER_LOG" | grep "时间:" | awk '{print $2}' | head -1)
        total_time=$(grep -A 1 "总计:" "$RECEIVER_LOG" | grep "时间:" | awk '{print $2}' | head -1)
        
        offline_comm=$(grep -A 2 "离线阶段:" "$RECEIVER_LOG" | grep "通信:" | awk '{print $2}' | head -1)
        online_comm=$(grep -A 2 "在线阶段:" "$RECEIVER_LOG" | grep "通信:" | awk '{print $2}' | head -1)
        total_comm=$(grep -A 2 "总计:" "$RECEIVER_LOG" | grep "通信:" | awk '{print $2}' | head -1)
        
        echo "  总时间: $total_time 秒 (离线: $offline_time s, 在线: $online_time s)"
        echo "  总通信: $total_comm MB (离线: $offline_comm MB, 在线: $online_comm MB)"
        echo ""
        
        # 保存到汇总文件
        echo "========================================" >> "$RESULTS_DIR/summary.txt"
        echo "测试配置: n=m=$size, d=$DIM, δ=$DELTA, L=$HASH_L" >> "$RESULTS_DIR/summary.txt"
        echo "----------------------------------------" >> "$RESULTS_DIR/summary.txt"
        echo "总时间: $total_time 秒" >> "$RESULTS_DIR/summary.txt"
        echo "  离线阶段: $offline_time 秒" >> "$RESULTS_DIR/summary.txt"
        echo "  在线阶段: $online_time 秒" >> "$RESULTS_DIR/summary.txt"
        echo "总通信: $total_comm MB" >> "$RESULTS_DIR/summary.txt"
        echo "  离线通信: $offline_comm MB" >> "$RESULTS_DIR/summary.txt"
        echo "  在线通信: $online_comm MB" >> "$RESULTS_DIR/summary.txt"
        echo "" >> "$RESULTS_DIR/summary.txt"
        
    else
        print_error "测试失败"
        echo "Receiver 退出码: $RECEIVER_EXIT"
        echo "Sender 退出码: $SENDER_EXIT"
        echo "Receiver 日志: $RECEIVER_LOG"
        echo "Sender 日志: $SENDER_LOG"
        echo ""
        echo "=== Receiver 日志内容 ==="
        tail -50 "$RECEIVER_LOG"
        echo ""
        echo "=== Sender 日志内容 ==="
        tail -50 "$SENDER_LOG"
    fi
    
    # 短暂休息，确保端口释放
    sleep 2
    echo ""
done

# 恢复原始文件
print_info "恢复原始源文件..."
mv fpsi_receiver.cpp.original fpsi_receiver.cpp
mv fpsi_sender.cpp.original fpsi_sender.cpp

print_header "所有测试完成"
print_info "结果保存在: $RESULTS_DIR/"
print_info "汇总文件: $RESULTS_DIR/summary.txt"

# 生成 CSV 报告
print_info "生成 CSV 报告..."
echo "SetSize,Dimension,Delta,HashFuncs,OfflineTime(s),OnlineTime(s),TotalTime(s),OfflineComm(MB),OnlineComm(MB),TotalComm(MB)" > "$RESULTS_DIR/results.csv"

for size in "${SET_SIZES[@]}"; do
    TEST_NAME="n${size}_d${DIM}_delta${DELTA}_L${HASH_L}"
    log="$RESULTS_DIR/receiver_${TEST_NAME}.log"
    
    if [ -f "$log" ]; then
        # 提取时间和通信数据
        offline_time=$(grep -A 1 "离线阶段:" "$log" | grep "时间:" | awk '{print $2}' | head -1)
        online_time=$(grep -A 1 "在线阶段:" "$log" | grep "时间:" | awk '{print $2}' | head -1)
        total_time=$(grep -A 1 "总计:" "$log" | grep "时间:" | awk '{print $2}' | head -1)
        offline_comm=$(grep -A 2 "离线阶段:" "$log" | grep "通信:" | awk '{print $2}' | head -1)
        online_comm=$(grep -A 2 "在线阶段:" "$log" | grep "通信:" | awk '{print $2}' | head -1)
        total_comm=$(grep -A 2 "总计:" "$log" | grep "通信:" | awk '{print $2}' | head -1)
        
        echo "$size,$DIM,$DELTA,$HASH_L,$offline_time,$online_time,$total_time,$offline_comm,$online_comm,$total_comm" >> "$RESULTS_DIR/results.csv"
    fi
done

print_info "CSV 报告已生成: $RESULTS_DIR/results.csv"
echo ""
echo "========================================"
echo "查看结果:"
echo "  汇总信息: cat $RESULTS_DIR/summary.txt"
echo "  CSV 数据: cat $RESULTS_DIR/results.csv"
echo "  详细日志: ls $RESULTS_DIR/*.log"
echo "========================================"
echo ""
echo "LaTeX 表格数据:"
echo "----------------------------------------"
cat "$RESULTS_DIR/results.csv" | tail -n +2 | while IFS=',' read -r size dim delta hash off_time on_time tot_time off_comm on_comm tot_comm; do
    echo "$size & Our \$\\Pi^{\\text{FHE}}_{\\text{FPSI}}\$ & $tot_comm & $tot_time \\\\"
done
echo "----------------------------------------"