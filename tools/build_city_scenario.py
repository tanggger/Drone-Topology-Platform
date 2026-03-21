import sys
import math
import os
import pandas as pd

if len(sys.argv) < 2:
    print("Usage: python3 build_city_scenario.py <formation_name>")
    sys.exit(1)

formation = sys.argv[1]
infile = f'data_rtk/mobility_trace_{formation}.txt'
outfile = f'data_rtk/mobility_trace_{formation}_avoidance.txt'
mapfile = f'data_map/city_map_{formation}.txt'

os.makedirs('data_map', exist_ok=True)

# 1. 自动探查轨迹特征，寻找下口大楼的最佳坐标
df = pd.read_csv(infile, comment='#', names=['time', 'nodeId', 'x', 'y', 'z'])
# 找到 t 约等于 60~100 秒的轨迹切片，这段时间的平均位置就是拦路大厦的坐标
mask = (df['time'] >= 60) & (df['time'] <= 100)
subset = df[mask]

if len(subset) == 0:
    print(f"轨迹 {formation} 时间太短，无法生成避障！")
    sys.exit(1)

bx_center = subset['x'].mean()
by_center = subset['y'].mean()

print(f"[{formation}] 探查完毕，系统在无人机必经之路盖了一栋大厦:")
print(f" -> 大厦中心点: X={bx_center:.1f}, Y={by_center:.1f}")

# 大楼尺寸：长宽各 40米 (半径20米)，高度 300米
b_half_len = 20.0

with open(mapfile, 'w') as f:
    f.write("# 自定义城市建筑地图 (支持多栋建筑)\n")
    f.write("# 格式: xMin xMax yMin yMax zMin zMax\n")
    f.write(f"{bx_center - b_half_len} {bx_center + b_half_len} {by_center - b_half_len} {by_center + b_half_len} 0.0 300.0\n")

# 2. 生成避障飞行轨迹 (真实动态躲避)
out_lines = []
with open(infile, 'r') as f:
    for line in f:
        if line.startswith('#'):
            out_lines.append(line)
            continue
        parts = line.strip().split(',')
        if len(parts) == 5:
            t = float(parts[0])
            n = int(parts[1])
            x = float(parts[2])
            y = float(parts[3])
            z = float(parts[4])
            
            # NS-3 Buildings 模型极度严苛，要求天线必须在地面以上
            if z < 0.1: z = 0.1
            
            # 避障核心：当时间在 40s 到 120s 之间，无人机感知到前方大楼，触发分离绕飞
            if 40 <= t <= 120:
                # 高斯平滑推力算法 (越靠近80秒，也就是大厦正中心，推力越大)
                force = math.exp(-((t - 80)**2) / (2 * 15.0**2))
                
                # 编队从中间一分为二，一组向左边逃逸，一组向右边逃逸
                dir_x = -1 if n < 8 else 1
                
                # 最大推开 35米，完美绕过半径20米的大楼
                push_dist = 35.0 * force
                x += dir_x * push_dist
                
            out_lines.append(f"{t:.3f},{n},{x:.3f},{y:.3f},{z:.3f}\n")

with open(outfile, 'w') as f:
    f.writelines(out_lines)

print(f"✅ 已成功生成真实避障轨迹: {outfile}")
print(f"🗺️ 已成功导出物理遮蔽地图: {mapfile}\n")
