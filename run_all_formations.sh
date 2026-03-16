#!/bin/bash
# ============================================================
# 算法对比批量仿真脚本
# 用法: chmod +x run_benchmark.sh && ./run_benchmark.sh
# ============================================================

# 编译两个版本的可执行文件
# 假设原版代码编译产物为 uav_old，修正版为 uav_new
# 根据你的 ns-3 构建方式调整路径
NS3_DIR="."  # ns-3 根目录

# ---- 配置 ----
FORMATIONS=("v_formation" "cross" "line" "triangle")
DIFFICULTIES=("Easy" "Moderate" "Hard")
DURATION=200
MAP_EASY=""
MAP_MODERATE="data_rtk/city_map_moderate.txt"
MAP_HARD="data_rtk/city_map_hard.txt"

# 根据难度选择地图文件
get_map() {
    local diff=$1
    case $diff in
        Easy)     echo "" ;;
        Moderate) echo "$MAP_MODERATE" ;;
        Hard)     echo "$MAP_HARD" ;;
    esac
}

echo "=========================================="
echo "  算法对比批量仿真 (共 $((${#FORMATIONS[@]} * ${#DIFFICULTIES[@]} * 3)) 次)"
echo "=========================================="

# # ============ 第1组: Static 基线 ============
# echo ""
# echo ">>> 第1组: Static 基线 (使用修正版代码, strategy=static)"
# for formation in "${FORMATIONS[@]}"; do
#     for difficulty in "${DIFFICULTIES[@]}"; do
#         map=$(get_map "$difficulty")
#         outdir="output/compare/static_${formation}_${difficulty}"
        
#         echo "[Static] formation=$formation difficulty=$difficulty"
        
#         cmd="./ns3 run 'uav_resource_allocation
#             --formation=$formation
#             --difficulty=$difficulty
#             --strategy=static
#             --duration=$DURATION
#             --outputDir=$outdir'"
        
#         # 如果有地图文件
#         if [ -n "$map" ]; then
#             cmd="./ns3 run 'uav_resource_allocation
#                 --formation=$formation
#                 --difficulty=$difficulty
#                 --strategy=static
#                 --duration=$DURATION
#                 --outputDir=$outdir
#                 --mapFile=$map'"
#         fi
        
#         eval $cmd 2>&1 | tail -5
#         echo "  -> 输出: $outdir"
#     done
# done

# # ============ 第2组: 原版 Dynamic ============
# echo ""
# echo ">>> 第2组: 原版 Dynamic (原代码编译, strategy=dynamic)"
# for formation in "${FORMATIONS[@]}"; do
#     for difficulty in "${DIFFICULTIES[@]}"; do
#         map=$(get_map "$difficulty")
#         outdir="output/compare/old_dynamic_${formation}_${difficulty}"
        
#         echo "[OldDynamic] formation=$formation difficulty=$difficulty"
        
#         cmd="./ns3 run 'uav_resource_allocation_old
#             --formation=$formation
#             --difficulty=$difficulty
#             --strategy=dynamic
#             --duration=$DURATION
#             --outputDir=$outdir'"
        
#         if [ -n "$map" ]; then
#             cmd="./ns3 run 'uav_resource_allocation_old
#                 --formation=$formation
#                 --difficulty=$difficulty
#                 --strategy=dynamic
#                 --duration=$DURATION
#                 --outputDir=$outdir
#                 --mapFile=$map'"
#         fi
        
#         eval $cmd 2>&1 | tail -5
#         echo "  -> 输出: $outdir"
#     done
# done

# ============ 第3组: 修正版 Dynamic ============
echo ""
echo ">>> 第3组: 修正版 Dynamic (修正代码编译, strategy=dynamic)"
for formation in "${FORMATIONS[@]}"; do
    for difficulty in "${DIFFICULTIES[@]}"; do
        map=$(get_map "$difficulty")
        outdir="output/compare/new_dynamic_${formation}_${difficulty}"
        
        echo "[NewDynamic] formation=$formation difficulty=$difficulty"
        
        cmd="./ns3 run 'uav_resource_allocation
            --formation=$formation
            --difficulty=$difficulty
            --strategy=dynamic
            --duration=$DURATION
            --outputDir=$outdir'"
        
        if [ -n "$map" ]; then
            cmd="./ns3 run 'uav_resource_allocation
                --formation=$formation
                --difficulty=$difficulty
                --strategy=dynamic
                --duration=$DURATION
                --outputDir=$outdir
                --mapFile=$map'"
        fi
        
        eval $cmd 2>&1 | tail -5
        echo "  -> 输出: $outdir"
    done
done

echo ""
echo "=========================================="
echo "  全部仿真完成！开始分析..."
echo "=========================================="

python3 analyze_comparison.py