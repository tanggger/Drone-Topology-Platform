#!/bin/bash
# ============================================================
# Phase 3 批量 Benchmark 脚本
# 一键跑完 4 种编队 × 3 种难度 = 12 种场景
#
# 使用方法:
#   bash run_benchmark.sh                        # 全部运行
#   bash run_benchmark.sh v_formation            # 只跑某编队的三种难度
#   bash run_benchmark.sh v_formation Hard       # 只跑某编队+难度
#
# 输出目录:
#   output/resource_allocation_<formation>_<difficulty>/
# ============================================================

NS3_DIR="$(cd "$(dirname "$0")" && pwd)"
DURATION=200

FORMATIONS=("v_formation" "cross" "line" "triangle")
DIFFICULTIES=("Easy" "Moderate" "Hard")

# 处理命令行参数
if [ -n "$1" ]; then FORMATIONS=("$1"); fi
if [ -n "$2" ]; then DIFFICULTIES=("$2"); fi

PASS=0; FAIL=0

for FORMATION in "${FORMATIONS[@]}"; do
    for DIFF in "${DIFFICULTIES[@]}"; do
        OUTPUT_DIR="$NS3_DIR/output/resource_allocation_${FORMATION}_${DIFF}"
        mkdir -p "$OUTPUT_DIR"

        echo ""
        echo "============================================================"
        echo "▶  编队: $FORMATION  |  难度: $DIFF"
        echo "   输出: $OUTPUT_DIR"
        echo "============================================================"

        cd "$NS3_DIR"
        ./ns3 run "uav_resource_allocation \
            --formation=$FORMATION \
            --difficulty=$DIFF \
            --duration=$DURATION \
            --outputDir=$OUTPUT_DIR" 2>&1

        if [ $? -eq 0 ]; then
            echo "✅ [$FORMATION $DIFF] 完成"
            PASS=$((PASS+1))
        else
            echo "❌ [$FORMATION $DIFF] 失败！"
            FAIL=$((FAIL+1))
        fi
    done
done

echo ""
echo "============================================================"
echo "🎉 全部完成！成功: $PASS  失败: $FAIL"
echo "============================================================"
ls -lh "$NS3_DIR/output/" | grep resource_allocation_
