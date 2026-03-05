#!/usr/bin/env python3
"""
完整的RTK到ns-3仿真测试脚本
测试从RTK数据生成到仿真运行的完整流程
"""

import os
import sys
import subprocess
import time
from pathlib import Path

def run_command(cmd, description, timeout=300, cwd=None):
    """运行命令并处理错误"""
    print(f"\n{'='*60}")
    print(f"执行: {description}")
    print(f"命令: {cmd}")
    if cwd:
        print(f"工作目录: {cwd}")
    print('='*60)
    
    start_time = time.time()
    try:
        # Using cwd parameter to run command in specified directory
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=timeout, cwd=cwd)
        elapsed = time.time() - start_time
        
        if result.returncode == 0:
            print(f"✓ 成功完成 (耗时: {elapsed:.1f}s)")
            if result.stdout:
                # 只显示最后几行输出，避免过长
                lines = result.stdout.strip().split('\n')
                if len(lines) > 10:
                    print("...")
                    for line in lines[-10:]:
                        print(line)
                else:
                    print(result.stdout)
        else:
            print(f"✗ 执行失败 (耗时: {elapsed:.1f}s)")
            print(f"错误信息: {result.stderr}")
            return False
            
    except subprocess.TimeoutExpired:
        print(f"✗ 超时失败 (超过{timeout}s)")
        return False
    except Exception as e:
        print(f"✗ 异常失败: {e}")
        return False
    
    return True

def check_file_exists(filepath, description):
    """检查文件是否存在"""
    if os.path.exists(filepath):
        size = os.path.getsize(filepath)
        print(f"✓ {description}: {filepath} ({size} bytes)")
        return True
    else:
        print(f"✗ {description}: {filepath} 不存在")
        return False

def main():
    print("RTK到ns-3仿真完整流程测试")
    print("=" * 60)
    
    # Determine paths based on the script's location
    script_path = Path(__file__).resolve()
    rtk_dir = script_path.parent
    project_root = rtk_dir.parent

    print(f"项目根目录: {project_root}")
    print(f"RTK目录: {rtk_dir}")
    
    # 步骤1: 生成RTK数据
    print(f"\n步骤1: 生成RTK数据")
    
    rtk_data_file = rtk_dir / 'test_rtk.csv'
    cmd = f"python3 {rtk_dir / 'generate_rtk_data.py'} --num_drones 20 --duration 100 --dt 0.2 --output {rtk_data_file}"
    if not run_command(cmd, "生成RTK测试数据"):
        return 1
    
    if not check_file_exists(rtk_data_file, "RTK数据文件"):
        return 1
    
    # 步骤2: 预处理RTK数据
    print(f"\n步骤2: 预处理RTK数据")
    processed_dir = rtk_dir / 'test_processed'
    processed_file = processed_dir / "processed_trajectories.csv"
    cmd = f"python3 {rtk_dir / 'preprocess.py'} --input {rtk_data_file} --output_dir {processed_dir} --dt 0.2"
    if not run_command(cmd, "预处理RTK数据"):
        return 1
    
    if not check_file_exists(processed_file, "预处理轨迹文件"):
        return 1
    
    # 步骤3: 生成ns-3轨迹文件
    print(f"\n步骤3: 生成ns-3轨迹文件")
    ns3_traces_dir = rtk_dir / 'test_ns3_traces'
    mobility_trace_file = ns3_traces_dir / "mobility_trace.txt"
    cmd = f"python3 {rtk_dir / 'generate_ns3_traces.py'} --input {processed_file} --output_dir {ns3_traces_dir}"
    if not run_command(cmd, "生成ns-3轨迹文件"):
        return 1
    
    if not check_file_exists(mobility_trace_file, "移动轨迹文件"):
        return 1
    
    # 步骤4: 编译ns-3仿真程序
    print(f"\n步骤4: 编译ns-3仿真程序")
    
    cmd = "./ns3 configure --enable-examples --enable-tests"
    if not run_command(cmd, "配置ns-3", cwd=project_root):
        return 1
    
    cmd = "./ns3 build rtk_simulation"
    if not run_command(cmd, "编译RTK仿真程序", timeout=600, cwd=project_root):
        return 1
    
    # 步骤5: 运行仿真
    print(f"\n步骤5: 运行仿真")
    cmd = f"./ns3 run 'rtk_simulation --trajectory={mobility_trace_file}'"
    if not run_command(cmd, "运行RTK仿真", timeout=600, cwd=project_root):
        return 1
    
    # 步骤6: 检查输出文件
    print(f"\n步骤6: 检查输出文件")
    output_files = [
        ("rtk-node-transmissions.csv", "传输事件文件"),
        ("rtk-topology-changes.txt", "拓扑变化文件"),
        ("rtk-node-positions.csv", "节点位置文件"),
        ("rtk-flowmon.xml", "FlowMonitor文件"),
        ("rtk-flow-stats.csv", "流统计文件")
    ]
    
    all_files_exist = True
    for filename, description in output_files:
        # Check for files in the project root where the simulation was run
        if not check_file_exists(project_root / filename, description):
            all_files_exist = False
    
    if all_files_exist:
        print(f"\n{'='*60}")
        print("🎉 完整流程测试成功！")
        print('='*60)
        print("\n生成的文件:")
        for filename, description in output_files:
            size = os.path.getsize(project_root / filename)
            print(f"  - {filename}: {description} ({size} bytes)")
        
        print(f"\n下一步可以:")
        print(f"1. 分析生成的CSV文件")
        print(f"2. 使用Python脚本生成通信概率拓扑图")
        print(f"3. 调整仿真参数重新运行")
        
        return 0
    else:
        print(f"\n❌ 部分文件生成失败")
        return 1

if __name__ == "__main__":
    sys.exit(main())
