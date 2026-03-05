#!/bin/bash
# 可视化工具使用示例脚本

# 设置颜色输出
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== 可视化工具使用示例 ===${NC}\n"

# 检查输入文件是否存在
SCENARIO="cross_Easy"
POSITIONS_FILE="benchmark/${SCENARIO}/node-positions.csv"
TOPOLOGY_FILE="benchmark/${SCENARIO}/topology-changes.txt"

if [ ! -f "$POSITIONS_FILE" ]; then
    echo "错误: 位置文件不存在: $POSITIONS_FILE"
    echo "请先运行仿真生成数据文件"
    exit 1
fi

if [ ! -f "$TOPOLOGY_FILE" ]; then
    echo "错误: 拓扑文件不存在: $TOPOLOGY_FILE"
    echo "请先运行仿真生成数据文件"
    exit 1
fi

# 创建输出目录
OUTPUT_DIR="visualization/output/${SCENARIO}"
mkdir -p "$OUTPUT_DIR"

echo -e "${GREEN}1. 绘制RTK轨迹图${NC}"
python3 visualization/plot_rtk_trajectory.py \
    "$POSITIONS_FILE" \
    --output-dir "$OUTPUT_DIR/rtk_trajectory"

echo -e "\n${GREEN}2. 绘制通信拓扑图${NC}"
python3 visualization/plot_topology.py \
    "$TOPOLOGY_FILE" \
    --positions "$POSITIONS_FILE" \
    --mode all \
    --output-dir "$OUTPUT_DIR/topology"

echo -e "\n${GREEN}3. 绘制组合图（轨迹+拓扑）${NC}"
python3 visualization/plot_combined.py \
    "$POSITIONS_FILE" \
    "$TOPOLOGY_FILE" \
    --view all \
    --output-dir "$OUTPUT_DIR/combined"

echo -e "\n${GREEN}完成！输出文件保存在: $OUTPUT_DIR${NC}"
