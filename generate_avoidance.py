#!/usr/bin/env python3
import sys
import math

infile = 'data_rtk/mobility_trace_v_formation.txt'
outfile = 'data_rtk/mobility_trace_avoidance.txt'

lines = []
with open(infile, 'r') as f:
    for line in f:
        if line.startswith('#'):
            lines.append(line)
            continue
        parts = line.strip().split(',')
        if len(parts) == 5:
            t = float(parts[0])
            n = int(parts[1])
            x = float(parts[2])
            y = float(parts[3])
            z = float(parts[4])
            
            # 编队在 t=60s 到 t=160s 之间遇到了巨大的障碍物 (比如大厦)
            # 我们引入人工势场排斥力，强行让无人机在左右 (X方向) 散开
            if 60 <= t <= 160:
                progress = (t - 60) / 100.0  # 0 to 1
                factor = math.sin(progress * math.pi)  # 平滑的半正弦曲线: 0 -> 1 -> 0
                
                # 节点 0-7 往负X方向推， 节点 8-14 往正X方向推
                # 推力逐渐增强，最大推开 50 米
                push_dist = 50.0 * factor
                if n < 8:
                    x -= push_dist
                else:
                    x += push_dist
                    
            lines.append(f"{t:.3f},{n},{x:.3f},{y:.3f},{z:.3f}\n")

with open(outfile, 'w') as f:
    f.writelines(lines)

print(f"✅ 已成功基于 V 字编队生成避障变形轨迹: {outfile}")
