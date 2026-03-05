import argparse
import pandas as pd
import numpy as np
import os

def parse_map(map_file):
    buildings = []
    if not os.path.exists(map_file):
        print(f"⚠️ 警告: 地图文件 {map_file} 不存在，将不使用任何建筑遮挡。")
        return buildings
    with open(map_file, 'r') as f:
        for line in f:
            if line.strip().startswith('#') or not line.strip():
                continue
            parts = line.split()
            if len(parts) >= 6:
                # [xMin, xMax, yMin, yMax, zMin, zMax]
                buildings.append((float(parts[0]), float(parts[1]), float(parts[2]), float(parts[3])))
    print(f"🏙️ 成功从 {map_file} 解析 {len(buildings)} 栋建筑物信息。")
    return buildings

def apply_elastic_band_avoidance(df, buildings, buffer=15.0, smooth_window=20):
    """
    使用弹性带算法(Elastic Band)和人工势场原理实现真正的动态避障。
    无论前端传回多少栋楼，都可以自动“推开”轨迹并进行平滑连接。
    """
    if len(buildings) == 0:
        return df

    print("🤖 启动数字孪生 AI 路径规划 (Elastic Band 算法)...")
    new_rows = []
    
    # 按照节点 ID 分组处理轨迹
    for drone_id, group in df.groupby('n'):
        group = group.sort_values('t').copy()
        
        x = group['x'].values.astype(float).copy()
        y = group['y'].values.astype(float).copy()
        
        # 增加极其微小的偏置，确保对称的直线编队遇到正中心的大楼时，能自然“分流”（一半向左，一半向右）
        bias = (drone_id % 2 - 0.5) * 0.01
        x += bias
        
        # 迭代 3 次: 推离障碍物 -> 平滑轨迹 -> 推离 -> 平滑
        for iteration in range(3):
            # 1. 人工势场排斥 (Push Step)
            for i in range(len(x)):
                cx, cy = x[i], y[i]
                for b in buildings:
                    # 设立建筑物的“排斥力场”缓冲区 (buffer)
                    bx1, bx2, by1, by2 = b[0]-buffer, b[1]+buffer, b[2]-buffer, b[3]+buffer
                    
                    # 如果这一个瞬间，飞机飞进了大楼的排斥力场
                    if bx1 < cx < bx2 and by1 < cy < by2:
                        # 找到距离最近的逃生边缘
                        dx1 = cx - bx1
                        dx2 = bx2 - cx
                        dy1 = cy - by1
                        dy2 = by2 - cy
                        
                        m = min(dx1, dx2, dy1, dy2)
                        # 将坐标强制推离大楼
                        if m == dx1: cx = bx1
                        elif m == dx2: cx = bx2
                        elif m == dy1: cy = by1
                        elif m == dy2: cy = by2
                
                x[i], y[i] = cx, cy
            
            # 2. 轨迹平滑 (Smooth Step) - 模拟真实的无人机动力学，避免瞬间闪现
            x = pd.Series(x).rolling(window=smooth_window, center=True, min_periods=1).mean().values.copy()
            y = pd.Series(y).rolling(window=smooth_window, center=True, min_periods=1).mean().values.copy()

        group['x'] = x
        group['y'] = y
        new_rows.append(group)
        print(f"  ✓ 节点 {drone_id} 独立避障航线计算完成")
        
    final_df = pd.concat(new_rows).sort_values(['t', 'n'])
    return final_df

def main():
    parser = argparse.ArgumentParser(description="前端实时避障转换器 (Digital Twin Path Planner)")
    parser.add_argument("--input", type=str, required=True, help="原始航线轨迹文件 (比如 data_rtk/mobility_trace_v_formation.txt)")
    parser.add_argument("--map", type=str, required=True, help="前端下发的实体地图文件 (比如 data_map/frontend_city.txt)")
    parser.add_argument("--output", type=str, required=True, help="自动避让后的新轨迹文件")
    
    args = parser.parse_args()
    
    # 1. 读入前端建筑物数据
    buildings = parse_map(args.map)
    
    # 2. 读入原始无脑直飞的轨迹数据
    print(f"📥 正在读取原始航线: {args.input}")
    try:
        df = pd.read_csv(args.input, comment='#', names=['t', 'n', 'x', 'y', 'z'])
    except Exception as e:
        print(f"❌ 读取错误: {e}")
        return

    # 3. 计算避障航线
    final_df = apply_elastic_band_avoidance(df, buildings, buffer=15.0, smooth_window=20)
    
    # 4. 导出给 NS-3
    # NS-3 要求有一定的格式，我们保留注释并覆写
    with open(args.output, 'w') as f:
        f.write("# 实时计算弹簧避障航线 (由前端地图驱动)\n")
        f.write("# Format: time, nodeId, x, y, z\n")
        
    final_df.to_csv(args.output, mode='a', index=False, header=False, float_format='%.3f')
    print(f"\n✅ 基于前端地图的真实飞行轨迹已生成完毕，并存入: {args.output}")
    print("✨ 下一步就可以直接把这个新文件和新地图喂给 NS-3 开始通信仿真的推演了！")

if __name__ == "__main__":
    main()
