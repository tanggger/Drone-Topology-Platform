#!/bin/bash
#
# test_installation.sh
# 
# 测试UAV资源分配仿真平台的安装
#

echo "========================================="
echo "UAV资源分配仿真平台安装测试"
echo "========================================="

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# NS-3目录 (自动获取脚本所在目录)
NS3_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo ""
echo "测试1: 检查NS-3目录..."
if [ -d "${NS3_DIR}" ]; then
    echo -e "${GREEN}✓${NC} NS-3目录存在"
else
    echo -e "${RED}✗${NC} NS-3目录不存在"
    exit 1
fi

echo ""
echo "测试2: 检查必需文件..."
FILES=(
    "scratch/uav-sim-helper.h"
    "scratch/uav-sim-helper.cc"
    "scratch/uav-resource-allocator.h"
    "scratch/uav-resource-allocator.cc"
    "scratch/uav_resource_allocation_advanced.cc"
    "uav_resource_allocation_config.ini"
    "visualize_results.py"
    "run_uav_simulation.sh"
    "UAV_README.md"
)

cd "${NS3_DIR}" || exit 1

ALL_EXIST=true
for file in "${FILES[@]}"; do
    if [ -f "${file}" ]; then
        echo -e "${GREEN}✓${NC} ${file}"
    else
        echo -e "${RED}✗${NC} ${file} 不存在"
        ALL_EXIST=false
    fi
done

if [ "${ALL_EXIST}" = false ]; then
    echo -e "\n${RED}部分文件缺失！${NC}"
    exit 1
fi

echo ""
echo "测试3: 检查可执行权限..."
if [ -x "run_uav_simulation.sh" ]; then
    echo -e "${GREEN}✓${NC} run_uav_simulation.sh 可执行"
else
    echo -e "${YELLOW}!${NC} run_uav_simulation.sh 不可执行，正在设置..."
    chmod +x run_uav_simulation.sh
fi

if [ -x "visualize_results.py" ]; then
    echo -e "${GREEN}✓${NC} visualize_results.py 可执行"
else
    echo -e "${YELLOW}!${NC} visualize_results.py 不可执行，正在设置..."
    chmod +x visualize_results.py
fi

echo ""
echo "测试4: 检查Python依赖..."
if command -v python3 &> /dev/null; then
    echo -e "${GREEN}✓${NC} Python3 已安装"
    
    # 检查必需的Python包
    python3 -c "import pandas" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓${NC} pandas 已安装"
    else
        echo -e "${YELLOW}!${NC} pandas 未安装，可视化功能可能不可用"
        echo "  安装命令: pip3 install pandas"
    fi
    
    python3 -c "import matplotlib" 2>/dev/null
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}✓${NC} matplotlib 已安装"
    else
        echo -e "${YELLOW}!${NC} matplotlib 未安装，可视化功能可能不可用"
        echo "  安装命令: pip3 install matplotlib"
    fi
else
    echo -e "${YELLOW}!${NC} Python3 未安装，可视化功能不可用"
fi

echo ""
echo "测试5: 检查NS-3编译状态..."
if [ -d "build" ]; then
    echo -e "${GREEN}✓${NC} build 目录存在"
    
    if [ -f "build/scratch/ns3.43-uav_resource_allocation_advanced-default" ]; then
        echo -e "${GREEN}✓${NC} 仿真程序已编译"
    else
        echo -e "${YELLOW}!${NC} 仿真程序未编译"
        echo "  需要运行: ./ns3 build"
    fi
else
    echo -e "${YELLOW}!${NC} NS-3未配置"
    echo "  需要运行: ./ns3 configure --enable-examples --enable-tests"
fi

echo ""
echo "测试6: 创建测试输出目录..."
TEST_OUTPUT="${NS3_DIR}/output/test"
mkdir -p "${TEST_OUTPUT}"
if [ -d "${TEST_OUTPUT}" ]; then
    echo -e "${GREEN}✓${NC} 测试输出目录创建成功"
    rm -rf "${TEST_OUTPUT}"
else
    echo -e "${RED}✗${NC} 无法创建输出目录"
    exit 1
fi

echo ""
echo "========================================="
echo "安装测试完成！"
echo "========================================="
echo ""
echo "后续步骤:"
echo "  1. 如果程序未编译，请运行:"
echo "     cd ${NS3_DIR}"
echo "     ./ns3 configure --enable-examples --enable-tests"
echo "     ./ns3 build"
echo ""
echo "  2. 运行示例仿真:"
echo "     ./run_uav_simulation.sh"
echo ""
echo "  3. 查看使用文档:"
echo "     cat UAV_README.md"
echo ""
echo "========================================="

