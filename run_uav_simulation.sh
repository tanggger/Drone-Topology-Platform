#!/bin/bash
#
# run_uav_simulation.sh
# 
# UAV资源分配仿真运行脚本
# 
# 用法:
#   ./run_uav_simulation.sh [strategy] [num_uavs] [num_channels]
#
# 示例:
#   ./run_uav_simulation.sh graph_coloring 15 3
#   ./run_uav_simulation.sh greedy 20 4
#

# 默认参数
STRATEGY=${1:-"graph_coloring"}
NUM_UAVS=${2:-15}
NUM_CHANNELS=${3:-3}
DURATION=${4:-200}

# NS-3配置
NS3_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="${NS3_DIR}/build"
# PROGRAM="scratch/uav_resource_allocation_advanced"
PROGRAM="scratch/uav_resource_allocation"

# 输出目录
OUTPUT_DIR="${NS3_DIR}/output/uav_resource_allocation_${STRATEGY}_${NUM_UAVS}uavs_${NUM_CHANNELS}ch"

echo "========================================="
echo "UAV资源分配仿真运行脚本"
echo "========================================="
echo "策略: ${STRATEGY}"
echo "UAV数量: ${NUM_UAVS}"
echo "信道数量: ${NUM_CHANNELS}"
echo "仿真时长: ${DURATION} 秒"
echo "输出目录: ${OUTPUT_DIR}"
echo "========================================="

# 创建输出目录
mkdir -p "${OUTPUT_DIR}"

# 进入NS-3目录
cd "${NS3_DIR}" || exit 1

# 检查是否需要编译
echo ""
echo "检查编译状态..."

# 自动通过 ns3 工具链进行构建管理 (移除硬编码的 configure，保留当前编译配置)
echo "正在调用 ns3 build..."
./ns3 build uav_resource_allocation

if [ $? -ne 0 ]; then
    echo "编译失败！"
    exit 1
fi
echo "编译/检查完成"

# if [ ! -f "${BUILD_DIR}/scratch/ns3.43-uav_resource_allocation-default" ]; then

# 运行仿真
echo ""
echo "开始运行仿真..."
echo "========================================="

./ns3 run "uav_resource_allocation \
    --strategy=${STRATEGY} \
    --numUAVs=${NUM_UAVS} \
    --numChannels=${NUM_CHANNELS} \
    --duration=${DURATION} \
    --outputDir=${OUTPUT_DIR}"

if [ $? -ne 0 ]; then
    echo ""
    echo "仿真运行失败！"
    exit 1
fi

echo ""
echo "========================================="
echo "仿真完成！"
echo "========================================="

# 生成可视化
echo ""
echo "生成可视化图表..."

if command -v python3 &> /dev/null; then
    python3 visualize_results.py "${OUTPUT_DIR}"
    
    if [ $? -eq 0 ]; then
        echo ""
        echo "可视化完成！"
        echo "图表位置: ${OUTPUT_DIR}/figures/"
    else
        echo ""
        echo "可视化失败，请手动运行:"
        echo "python3 visualize_results.py ${OUTPUT_DIR}"
    fi
else
    echo "未找到Python3，跳过可视化"
    echo "若需要可视化，请手动运行:"
    echo "python3 visualize_results.py ${OUTPUT_DIR}"
fi

echo ""
echo "========================================="
echo "全部完成！"
echo "结果保存在: ${OUTPUT_DIR}"
echo "========================================="

