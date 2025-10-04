#!/bin/bash

# FPSI 性能测试脚本
# 测试不同数据集大小: 2^10, 2^12, 2^14

set -e

# 颜色定义
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
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

# 检查可执行文件
if [ ! -f "build/fpsi_receiver" ] || [ ! -f "build/fpsi_sender" ]; then
    echo "错误: 找不到可执行文件，请先编译项目"
    echo "运行: ./build.sh"
    exit 1
fi

# 创建结果目录
RESULTS_DIR="benchmark_results_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$RESULTS_DIR"

print_header "FPSI 性能测试开始"
echo "结果将保存到: $RESULTS_DIR"
echo ""

# 测试配置
PORT=12345
DIMENSIONS=(128)           # 向量维度
DELTAS=(10)                # 汉明距离阈值
HASH_FUNCS=(32)            # 哈希函数数量
SET_SIZES=(1024 4096 16384)  # 2^10, 2^12, 2^14

# 运行测试
test_count=0
total_tests=$((${#SET_SIZES[@]} * ${#DIMENSIONS[@]} * ${#DELTAS[@]} * ${#HASH_FUNCS[@]}))

for size in "${SET_SIZES[@]}"; do
    for dim in "${DIMENSIONS[@]}"; do
        for delta in "${DELTAS[@]}"; do
            for hash_l in "${HASH_FUNCS[@]}"; do
                test_count=$((test_count + 1))
                
                print_test "测试 $test_count/$total_tests"
                echo "  参数: n=$size, m=$size, d=$dim, δ=$delta, L=$hash_l"
                
                # 生成测试文件名
                TEST_NAME="n${size}_d${dim}_delta${delta}_L${hash_l}"
                RECEIVER_LOG="$RESULTS_DIR/receiver_${TEST_NAME}.log"
                SENDER_LOG="$RESULTS_DIR/sender_${TEST_NAME}.log"
                
                # 创建临时修改的源文件
                print_info "准备测试配置..."
                
                # 备份原文件
                cp fpsi_receiver.cpp fpsi_receiver.cpp.bak
                cp fpsi_sender.cpp fpsi_sender.cpp.bak
                
                # 修改 receiver 参数
                sed -i "s/int n = [0-9]*;/int n = $size;/" fpsi_receiver.cpp
                sed -i "s/int d = [0-9]*;/int d = $dim;/" fpsi_receiver.cpp
                sed -i "s/int delta = [0-9]*;/int delta = $delta;/" fpsi_receiver.cpp
                sed -i "s/int L = [0-9]*;/int L = $hash_l;/" fpsi_receiver.cpp
                
                # 修改 sender 参数
                sed -i "s/int m = [0-9]*;/int m = $size;/" fpsi_sender.cpp
                sed -i "s/int d = [0-9]*;/int d = $dim;/" fpsi_sender.cpp
                sed -i "s/int delta = [0-9]*;/int delta = $delta;/" fpsi_sender.cpp
                sed -i "s/int L = [0-9]*;/int L = $hash_l;/" fpsi_sender.cpp
                
                # 重新编译
                print_info "重新编译..."
                cd build
                make -j$(nproc) > /dev/null 2>&1
                cd ..
                
                # 启动 receiver
                print_info "启动 Receiver..."
                ./build/fpsi_receiver $PORT > "$RECEIVER_LOG" 2>&1 &
                RECEIVER_PID=$!
                
                # 等待 receiver 启动
                sleep 2
                
                # 检查 receiver 是否运行
                if ! ps -p $RECEIVER_PID > /dev/null; then
                    echo "错误: Receiver 启动失败"
                    cat "$RECEIVER_LOG"
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
                    
                    # 提取统计信息
                    echo "--- Receiver 统计 ---" >> "$RESULTS_DIR/summary.txt"
                    grep -A 10 "统计信息" "$RECEIVER_LOG" >> "$RESULTS_DIR/summary.txt"
                    echo "" >> "$RESULTS_DIR/summary.txt"
                    
                    echo "--- Sender 统计 ---" >> "$RESULTS_DIR/summary.txt"
                    grep -A 10 "统计信息" "$SENDER_LOG" >> "$RESULTS_DIR/summary.txt"
                    echo "" >> "$RESULTS_DIR/summary.txt"
                    echo "========================================" >> "$RESULTS_DIR/summary.txt"
                    echo "" >> "$RESULTS_DIR/summary.txt"
                else
                    echo "错误: 测试失败"
                    echo "Receiver 日志: $RECEIVER_LOG"
                    echo "Sender 日志: $SENDER_LOG"
                fi
                
                # 恢复原文件
                mv fpsi_receiver.cpp.bak fpsi_receiver.cpp
                mv fpsi_sender.cpp.bak fpsi_sender.cpp
                
                # 短暂休息
                sleep 1
                echo ""
            done
        done
    done
done

print_header "所有测试完成"
print_info "结果保存在: $RESULTS_DIR/"
print_info "汇总文件: $RESULTS_DIR/summary.txt"

# 生成 CSV 报告
print_info "生成 CSV 报告..."
echo "SetSize,Dimension,Delta,HashFuncs,OfflineTime,OnlineTime,TotalTime,OfflineComm,OnlineComm,TotalComm" > "$RESULTS_DIR/results.csv"

for log in "$RESULTS_DIR"/receiver_*.log; do
    if [ -f "$log" ]; then
        # 从文件名提取参数
        basename=$(basename "$log" .log)
        params=$(echo "$basename" | sed 's/receiver_n//' | sed 's/_d/,/' | sed 's/_delta/,/' | sed 's/_L/,/')
        
        # 提取时间和通信数据
        offline_time=$(grep "离线阶段:" -A 1 "$log" | grep "时间:" | awk '{print $2}')
        online_time=$(grep "在线阶段:" -A 1 "$log" | grep "时间:" | awk '{print $2}')
        total_time=$(grep "总计:" -A 1 "$log" | grep "时间:" | awk '{print $2}')
        offline_comm=$(grep "离线阶段:" -A 2 "$log" | grep "通信:" | awk '{print $2}')
        online_comm=$(grep "在线阶段:" -A 2 "$log" | grep "通信:" | awk '{print $2}')
        total_comm=$(grep "总计:" -A 2 "$log" | grep "通信:" | awk '{print $2}')
        
        echo "$params,$offline_time,$online_time,$total_time,$offline_comm,$online_comm,$total_comm" >> "$RESULTS_DIR/results.csv"
    fi
done

print_info "CSV 报告已生成: $RESULTS_DIR/results.csv"
echo ""
echo "查看结果:"
echo "  详细日志: ls $RESULTS_DIR/*.log"
echo "  汇总信息: cat $RESULTS_DIR/summary.txt"
echo "  CSV 数据: cat $RESULTS_DIR/results.csv"