#!/bin/bash
#
# compare_strategies.sh
# 
# 对比不同资源分配策略的性能
#

echo "========================================="
echo "UAV资源分配策略性能对比实验"
echo "========================================="

# 实验参数
NUM_UAVS=15
NUM_CHANNELS=3
DURATION=200

# 策略列表
STRATEGIES=("static" "greedy" "graph_coloring" "interference_aware")

# NS-3目录
NS3_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "${NS3_DIR}" || exit 1

# 创建对比结果目录
COMPARE_DIR="${NS3_DIR}/output/strategy_comparison_$(date +%Y%m%d_%H%M%S)"
mkdir -p "${COMPARE_DIR}"

echo ""
echo "实验参数:"
echo "  UAV数量: ${NUM_UAVS}"
echo "  信道数量: ${NUM_CHANNELS}"
echo "  仿真时长: ${DURATION} 秒"
echo "  输出目录: ${COMPARE_DIR}"
echo ""

# 运行每个策略
for strategy in "${STRATEGIES[@]}"; do
    echo "========================================="
    echo "运行策略: ${strategy}"
    echo "========================================="
    
    OUTPUT_DIR="${COMPARE_DIR}/${strategy}"
    
    ./ns3 run "uav_resource_allocation_advanced \
        --strategy=${strategy} \
        --numUAVs=${NUM_UAVS} \
        --numChannels=${NUM_CHANNELS} \
        --duration=${DURATION} \
        --outputDir=${OUTPUT_DIR}"
    
    if [ $? -eq 0 ]; then
        echo "✓ ${strategy} 完成"
        
        # 生成可视化
        if command -v python3 &> /dev/null; then
            python3 visualize_results.py "${OUTPUT_DIR}" > /dev/null 2>&1
        fi
    else
        echo "✗ ${strategy} 失败"
    fi
    
    echo ""
done

# 生成对比报告
echo "========================================="
echo "生成对比报告"
echo "========================================="

REPORT_FILE="${COMPARE_DIR}/comparison_report.txt"

cat > "${REPORT_FILE}" << EOF
========================================
UAV资源分配策略性能对比报告
========================================

实验配置:
  UAV数量: ${NUM_UAVS}
  信道数量: ${NUM_CHANNELS}
  仿真时长: ${DURATION} 秒
  
========================================
性能对比
========================================

EOF

# 提取各个策略的性能指标
for strategy in "${STRATEGIES[@]}"; do
    SUMMARY_FILE="${COMPARE_DIR}/${strategy}/summary.txt"
    
    if [ -f "${SUMMARY_FILE}" ]; then
        echo "策略: ${strategy}" >> "${REPORT_FILE}"
        echo "----------------------------------------" >> "${REPORT_FILE}"
        
        # 提取关键指标
        grep -E "平均PDR|平均时延|总吞吐量" "${SUMMARY_FILE}" >> "${REPORT_FILE}" 2>/dev/null
        
        echo "" >> "${REPORT_FILE}"
    fi
done

echo "========================================" >> "${REPORT_FILE}"

# 显示报告
cat "${REPORT_FILE}"

echo ""
echo "========================================="
echo "对比实验完成！"
echo "========================================="
echo "结果保存在: ${COMPARE_DIR}"
echo "对比报告: ${REPORT_FILE}"
echo "========================================="

