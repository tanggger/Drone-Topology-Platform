#!/bin/bash
# ============================================================
# 批量运行 4 种编队仿真，生成数据包说明.txt 中对应的所有输出
#
# 使用方法：
#   bash run_all_formations.sh              # 运行全部 4 种编队
#   bash run_all_formations.sh v_formation  # 只运行 V 字编队
#
# 输出目录结构：
#   output/cross/          ← 十字形编队
#   output/line/           ← 直线形编队
#   output/triangle/       ← 三角形编队
#   output/v_formation/    ← V字形编队
# ============================================================

NS3_DIR="$(cd "$(dirname "$0")" && pwd)"
TRACE_DIR="$NS3_DIR/data_rtk"
OUTPUT_ROOT="$NS3_DIR/output"

# 编队名称 → 对应的轨迹文件
declare -A TRACE_FILES=(
    ["cross"]="$TRACE_DIR/mobility_trace_cross.txt"
    ["line"]="$TRACE_DIR/mobility_trace_line.txt"
    ["triangle"]="$TRACE_DIR/mobility_trace_triangle.txt"
    ["v_formation"]="$TRACE_DIR/mobility_trace_v_formation.txt"
)

# 如果指定了参数则只跑该编队，否则跑全部
if [ -n "$1" ]; then
    FORMATIONS=("$1")
else
    FORMATIONS=("cross" "line" "triangle" "v_formation")
fi

# ── 主循环 ──────────────────────────────────────────────────
for FORMATION in "${FORMATIONS[@]}"; do

    TRACE_FILE="${TRACE_FILES[$FORMATION]}"

    # 检查编队名是否合法
    if [ -z "$TRACE_FILE" ]; then
        echo "❌ 未知编队名: $FORMATION"
        echo "   可选: cross / line / triangle / v_formation"
        exit 1
    fi

    # 检查轨迹文件是否存在
    if [ ! -f "$TRACE_FILE" ]; then
        echo "❌ 轨迹文件不存在: $TRACE_FILE"
        exit 1
    fi

    OUTPUT_DIR="$OUTPUT_ROOT/$FORMATION"
    mkdir -p "$OUTPUT_DIR"

    echo ""
    echo "============================================================"
    echo "🚀 运行编队仿真: $FORMATION"
    echo "   轨迹文件: $TRACE_FILE"
    echo "   输出目录: $OUTPUT_DIR"
    echo "============================================================"

    # 运行 NS-3 仿真
    cd "$NS3_DIR"
    ./ns3 run "rtk_simulation \
        --trajectory=$TRACE_FILE \
        --outputDir=$OUTPUT_DIR"

    if [ $? -eq 0 ]; then
        echo ""
        echo "✅ [$FORMATION] 仿真完成，已生成以下文件:"
        for f in rtk-node-positions.csv rtk-node-transmissions.csv rtk-flow-stats.csv rtk-topology-changes.txt rtk-flowmon.xml; do
            FULLPATH="$OUTPUT_DIR/$f"
            if [ -f "$FULLPATH" ]; then
                SIZE=$(du -h "$FULLPATH" | cut -f1)
                echo "   ✓ $f  ($SIZE)"
            else
                echo "   ✗ $f  (未生成！)"
            fi
        done
    else
        echo "❌ [$FORMATION] 仿真运行失败！"
        exit 1
    fi

done

echo ""
echo "============================================================"
echo "🎉 全部编队仿真完成！输出目录: $OUTPUT_ROOT"
echo "============================================================"
ls -lh "$OUTPUT_ROOT"/
