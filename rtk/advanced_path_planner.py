#!/usr/bin/env python3
"""
无人机集群轨迹规划引擎 v3.1
════════════════════════════
v3.0 → v3.1 修复:
  [F1] A*边距: 编队半径+15 → 单机间距8m (让路径能穿楼隙)
  [F2] A*代价: 增加偏离直线惩罚 (鼓励直飞)
  [F3] 编队朝向: 静态旋转矩阵 → 实时跟踪前进方向, 带角速度限制
  [F4] 自适应边距: 8m无路 → 逐步放宽 → 实在不行才绕
  [F5] 编队压缩联动: 窄道自动收缩编队, 通过后恢复
"""

import numpy as np
import pandas as pd
import math
import os
import heapq
from datetime import datetime, timedelta
import argparse


# ════════════════════════════════════════════════════════════════
#  A* 全局路径规划器 (含直线偏好)
# ════════════════════════════════════════════════════════════════
class GridPathPlanner:
    """
    [F1] 使用小边距 (单机尺度) 让路径能穿过楼隙
    [F2] 加入偏离直线惩罚, 优先走直线方向
    [F4] 自适应边距: 从小到大尝试, 找到最直接的路径
    """

    def __init__(self, buildings, start, target,
                 resolution=5.0, safety_margin=8.0,
                 deviation_weight=0.3):
        self.res = resolution
        self.dev_w = deviation_weight
        
        # 3D 规划参数
        # 允许的最大爬升高度: 60m (或更高，根据场景高度)
        self.max_climb_z = 60.0
        # 垂直机动代价惩罚系数 (爬升很费电，所以代价要比水平绕高)
        self.vertical_cost_factor = 2.5 

        # 3D 坐标提取
        self.start_pos = np.array(start, dtype=float)
        self.target_pos = np.array(target, dtype=float)
        
        # 直线参考 (起点→终点) - 2D投影用于启发式引导
        self.flight_height = self.start_pos[2]  
        self.line_start = self.start_pos[:2]
        self.line_end = self.target_pos[:2]
        line_vec = self.line_end - self.line_start
        self.line_len = np.linalg.norm(line_vec)
        if self.line_len > 0.01:
            self.line_dir = line_vec / self.line_len
        else:
            self.line_dir = np.array([0.0, 1.0])

        # 栅格边界 (XY平面)
        xs = [start[0], target[0]]
        ys = [start[1], target[1]]
        # 收集场景最高建筑物, 决定Z轴层数
        max_building_h = self.flight_height
        for b in buildings:
            xs += [b['xmin'], b['xmax']]
            ys += [b['ymin'], b['ymax']]
            max_building_h = max(max_building_h, b.get('zmax', 0))
            
        # 减少网格边界留白，防止路径过度向外绕行 (150 -> 40)
        pad = 40.0
        self.x0 = min(xs) - pad
        self.x1 = max(xs) + pad
        self.y0 = min(ys) - pad
        self.y1 = max(ys) + pad
        self.nx = int((self.x1 - self.x0) / self.res) + 1
        self.ny = int((self.y1 - self.y0) / self.res) + 1
        
        # Z轴栅格化 (层高也用 resolution，或者更细)
        # 范围: [0, max(start_z, max_building_h + 10)]
        self.z_res = resolution 
        self.max_z_layer = max(self.flight_height, max_building_h) + 20.0
        self.nz = int(self.max_z_layer / self.z_res) + 1

        # 构建 3D 占据栅格 [nx, ny, nz]
        print(f"🏗️  构建 3D 栅格地图: {self.nx}x{self.ny}x{self.nz} (Res: {self.res}m)")
        self.grid = np.zeros((self.nx, self.ny, self.nz), dtype=bool)
        self._inflate(buildings, safety_margin)

        occ = np.sum(self.grid)
        tot = self.nx * self.ny * self.nz
        print(f"🗺️  3D 障碍 {occ} ({100 * occ / max(tot, 1):.1f}%) | "
              f"边距 {safety_margin:.0f}m")

        self.waypoints = self._plan(start, target)
        print(f"📍 航路点 {len(self.waypoints)}:")
        for k, wp in enumerate(self.waypoints):
            print(f"    WP{k}: ({wp[0]:.1f}, {wp[1]:.1f}, {wp[2]:.1f})")

    def _inflate(self, buildings, margin):
        """膨胀建筑物 (3D 体素化)"""
        self.grid[:] = False
        
        for b in buildings:
            # XY 平面范围
            i1 = max(0, int((b['xmin'] - margin - self.x0) / self.res))
            i2 = min(self.nx - 1, int((b['xmax'] + margin - self.x0) / self.res))
            j1 = max(0, int((b['ymin'] - margin - self.y0) / self.res))
            j2 = min(self.ny - 1, int((b['ymax'] + margin - self.y0) / self.res))
            
            # Z 轴范围 (从地面 0 到 zmax + margin)
            # zmin 通常是 0，但也支持悬空障碍
            z_bottom = b.get('zmin', 0)
            z_top = b.get('zmax', 10.0)
            
            k1 = max(0, int((z_bottom - margin) / self.z_res))
            k2 = min(self.nz - 1, int((z_top + margin) / self.z_res))
            
            # 标记体素为障碍
            self.grid[i1:i2 + 1, j1:j2 + 1, k1:k2 + 1] = True

    def _w2g(self, pos):
        """世界坐标 -> 3D 栅格坐标"""
        return (int(np.clip((pos[0] - self.x0) / self.res, 0, self.nx - 1)),
                int(np.clip((pos[1] - self.y0) / self.res, 0, self.ny - 1)),
                int(np.clip(pos[2] / self.z_res, 0, self.nz - 1)))

    def _g2w(self, idx):
        """3D 栅格坐标 -> 世界坐标"""
        return np.array([
            self.x0 + (idx[0] + 0.5) * self.res,
            self.y0 + (idx[1] + 0.5) * self.res,
            (idx[2] + 0.5) * self.z_res
        ])


    def _nearest_free(self, i, j):
        if 0 <= i < self.nx and 0 <= j < self.ny and not self.grid[i, j]:
            return i, j
        for r in range(1, max(self.nx, self.ny)):
            for di in range(-r, r + 1):
                for dj in range(-r, r + 1):
                    if abs(di) != r and abs(dj) != r:
                        continue
                    ni, nj = i + di, j + dj
                    if 0 <= ni < self.nx and 0 <= nj < self.ny \
                            and not self.grid[ni, nj]:
                        return ni, nj
        return i, j

    def _perp_dist(self, world_pos):
        """[F2] 点到 起点→终点 直线 的垂直距离 (2D平面投影)"""
        v = world_pos[:2] - self.line_start
        proj_len = np.dot(v, self.line_dir)
        proj = proj_len * self.line_dir
        perp = v - proj
        return np.linalg.norm(perp)

    def _plan(self, start, target):
        """A* 3D 路径规划 (支持水平/垂直/斜向机动)"""
        start = np.array(start, dtype=float)
        target = np.array(target, dtype=float)
        
        # 1. 转换为 3D 栅格坐标
        si, sj, sk = self._w2g(start)
        gi, gj, gk = self._w2g(target)
        
        # 2. 如果起终点在障碍物内，寻找最近的自由点 (3D搜索)
        # 这里简化处理，假设起终点是自由的，或者只需简单的水平偏移
        if self.grid[si, sj, sk]:
             print(f"⚠️ 起点 ({si},{sj},{sk}) 在障碍物内，需调整...")
             # 简单策略：向上抬升直到自由
             while sk < self.nz - 1 and self.grid[si, sj, sk]:
                 sk += 1
        
        if self.grid[gi, gj, gk]:
             print(f"⚠️ 终点 ({gi},{gj},{gk}) 在障碍物内，需调整...")
             while gk < self.nz - 1 and self.grid[gi, gj, gk]:
                 gk += 1

        # 3. 初始化 A*
        # Heap item: (f_score, tie_breaker, x, y, z)
        heap = []
        heapq.heappush(heap, (0.0, 0, si, sj, sk))
        
        come_from = {} # key: (x,y,z), val: (px,py,pz)
        g_score = {}   # key: (x,y,z), val: cost
        
        start_node = (si, sj, sk)
        end_node = (gi, gj, gk)
        
        g_score[start_node] = 0.0
        came_from = {start_node: None}
        
        cnt = 0
        
        # 定义 3D 邻居动作 (26邻域或简化版)
        # 简化版动作集: 
        # - 水平8方向 (代价 1 或 1.414)
        # - 纯垂直2方向 (代价 1 * vertical_factor)
        # - 简单的斜向爬升 (如前进一步同时爬升一步)
        MOVES = []
        # 水平移动
        for dx in [-1, 0, 1]:
            for dy in [-1, 0, 1]:
                if dx==0 and dy==0: continue
                dist = np.sqrt(dx**2 + dy**2)
                MOVES.append((dx, dy, 0, dist))
        
        # 垂直移动 (爬升/下降)
        MOVES.append((0, 0, 1, 1.0 * self.vertical_cost_factor))
        MOVES.append((0, 0, -1, 1.0 * self.vertical_cost_factor))
        
        # 斜向机动 (可选，增加灵活性)
        for dx in [-1, 1]:
            for dy in [-1, 1]:
                # 前进同时爬升/下降
                dist = np.sqrt(dx**2 + dy**2 + 1)
                cost = dist * (1.0 + 0.5 * self.vertical_cost_factor) # 混合代价
                MOVES.append((dx, dy, 1, cost))
                MOVES.append((dx, dy, -1, cost))

        found = False
        final_node = None
        
        while heap:
            current_f, _, cx, cy, cz = heapq.heappop(heap)
            
            if (cx, cy, cz) == end_node:
                found = True
                final_node = (cx, cy, cz)
                break
            
            # 提前剪枝: 如果当前 f_score 已经远超已知的最优 g (虽对于 A* 不常见)
            
            for dx, dy, dz, move_cost in MOVES:
                nx, ny, nz = cx + dx, cy + dy, cz + dz
                
                # 越界检查
                if not (0 <= nx < self.nx and 0 <= ny < self.ny and 0 <= nz < self.nz):
                    continue
                
                # 障碍物检查
                if self.grid[nx, ny, nz]:
                    continue
                
                # 计算 G score
                # 增加直线偏离惩罚
                curr_w_pos = self._g2w((nx, ny, nz))
                dev = self._perp_dist(curr_w_pos)
                dev_penalty = self.dev_w * (dev / max(self.line_len * 0.5, 1.0))
                
                tentative_g = g_score[(cx, cy, cz)] + move_cost + dev_penalty
                
                next_node = (nx, ny, nz)
                if next_node not in g_score or tentative_g < g_score[next_node]:
                    g_score[next_node] = tentative_g
                    # H score: 3D 欧氏距离 + 垂直惩罚
                    h_dist = np.sqrt((nx-gi)**2 + (ny-gj)**2)
                    v_dist = abs(nz-gk) * self.vertical_cost_factor
                    f_score = tentative_g + h_dist + v_dist
                    
                    cnt += 1
                    heapq.heappush(heap, (f_score, cnt, nx, ny, nz))
                    came_from[next_node] = (cx, cy, cz)

        if not found:
            print("❌ A* 3D 寻路失败，尝试直接连接起终点...")
            return [start, target]
            
        # 回溯路径
        path = []
        curr = final_node
        while curr is not None:
            path.append(self._g2w(curr))
            curr = came_from[curr]
        path.reverse()
        
        return path

    def _smooth(self, path):
        if len(path) <= 2:
            return path
        out = [path[0]]
        i = 0
        while i < len(path) - 1:
            best = i + 1
            for j in range(len(path) - 1, i + 1, -1):
                if self._los(path[i], path[j]):
                    best = j
                    break
            out.append(path[best])
            i = best
        return out

    def _los(self, a, b):
        d = np.linalg.norm(b[:2] - a[:2])
        n = max(3, int(d / (self.res * 0.5)))
        for s in range(n + 1):
            t = s / n
            p = a * (1 - t) + b * t
            ix, iy = self._w2g(p)
            if self.grid[ix, iy]:
                return False
        return True


def plan_with_adaptive_margin(buildings, start, target,
                              resolution=5.0, dev_weight=0.3):
    """
    [F4] 自适应边距: 从 8m 开始尝试, 找不到再放宽, 尽量走直线
    """
    margins = [8.0, 15.0, 25.0, 40.0, 60.0]
    for m in margins:
        print(f"   🔍 尝试边距 {m:.0f}m ...")
        planner = GridPathPlanner(buildings, start, target,
                                  resolution, m, dev_weight)
        if planner.waypoints is not None:
            # 计算路径总长
            total = sum(np.linalg.norm(
                planner.waypoints[i+1] - planner.waypoints[i])
                for i in range(len(planner.waypoints) - 1))
            direct = np.linalg.norm(
                np.array(target) - np.array(start))
            ratio = total / max(direct, 1)
            print(f"   ✅ 边距 {m:.0f}m 成功 | "
                  f"路径长 {total:.0f}m / 直线 {direct:.0f}m "
                  f"= {ratio:.2f}x")
            return planner.waypoints
    print("   ⚠️ 所有边距均失败, 使用直线备用")
    return [np.array(start, dtype=float), np.array(target, dtype=float)]


# ════════════════════════════════════════════════════════════════
#  主集群引擎
# ════════════════════════════════════════════════════════════════
class AdvancedDroneSwarm:

    def __init__(self, num_drones=15, start_pos=(0, 0, 30),
                 target_pos=(0, 500, 30), formation="v_formation",
                 spacing=12.0, map_file=None, dt=0.1):
        self.num_drones = num_drones
        self.start_pos = np.array(start_pos, dtype=float)
        self.target_pos = np.array(target_pos, dtype=float)
        self.formation_type = formation
        self.dt = dt

        # 物理
        self.max_speed = 15.0
        self.max_force = 12.0

        # 避障
        self.detect_range = 60.0
        self.hard_range = 5.0
        self.avoid_gain = 600.0
        self.lookahead_t = 3.0
        self.default_bld_h = 80.0

        # 编队
        self.formation_gap = float(spacing)
        self.sep_trigger = self.formation_gap * 0.5

        # GPS
        self.base_lat, self.base_lon, self.base_alt = 39.9042, 116.4074, 50.0

        # 建筑物
        self.buildings = self._load_map(map_file)

        # 航向
        d = self.target_pos - self.start_pos
        self.mission_dist = np.linalg.norm(d)
        init_heading = math.atan2(d[1], d[0]) if self.mission_dist > 0 else math.pi / 2

        # [F3] 动态航向状态 (不再固定)
        self.current_heading = init_heading
        self.heading_rate_limit = math.radians(20)  # 最大 45°/s 转弯

        # 编队原始偏移 (局部坐标, Y=前方, X=侧方)
        self.base_offsets = self._gen_offsets()

        self.max_radius = max(
            np.linalg.norm(self.base_offsets[i][:2])
            for i in range(self.num_drones))

        # [F1/F4] A* 全局路径 (用小边距 + 自适应)
        if self.buildings:
            print(f"📐 编队半径 {self.max_radius:.0f}m (小边距 A* 穿楼隙)")
            self.waypoints = plan_with_adaptive_margin(
                self.buildings, start_pos, target_pos,
                resolution=5.0, dev_weight=0.3)
            if len(self.waypoints) > 2:
                self.waypoints = self._simplify_waypoints(self.waypoints)
        else:
            self.waypoints = [self.start_pos.copy(), self.target_pos.copy()]
        self.wp_idx = 0

        # 初始化旋转后的偏移
        self.current_offsets = self._rotate_offsets(self.current_heading)

        # 无人机初始状态
        self.pos = np.zeros((self.num_drones, 3))
        self.vel = np.zeros((self.num_drones, 3))
        for i in range(self.num_drones):
            self.pos[i] = self.start_pos + self.current_offsets[i]
            if self.mission_dist > 0:
                self.vel[i] = (d / self.mission_dist) * self.max_speed * 0.3

        # 领航者
        self.leader_pos = self.start_pos.copy()
        self.leader_vel = np.zeros(3)
        self.leader_base_spd = self.max_speed * 0.6

        # 停滞检测
        self.stag_cnt = np.zeros(self.num_drones)
        self.prev_pos = self.pos.copy()

        # 编队缩放
        self.form_scale = 1.0
        self.form_scale_target = 1.0


    def _simplify_waypoints(self, path, angle_threshold=15.0):
        """
        合并近似共线的航路点，减少不必要的转弯
        只在转弯角度超过阈值时才保留航路点
        """
        if len(path) <= 2:
            return path
        
        simplified = [path[0]]
        thresh_rad = math.radians(angle_threshold)
        
        for i in range(1, len(path) - 1):
            v1 = path[i] - path[i-1]
            v2 = path[i+1] - path[i]
            
            n1 = np.linalg.norm(v1[:2])
            n2 = np.linalg.norm(v2[:2])
            
            if n1 < 0.01 or n2 < 0.01:
                continue
                
            cos_angle = np.dot(v1[:2], v2[:2]) / (n1 * n2)
            cos_angle = np.clip(cos_angle, -1, 1)
            angle = math.acos(cos_angle)
            
            if angle > thresh_rad:
                simplified.append(path[i])
        
        simplified.append(path[-1])
        print(f"   📐 路径简化: {len(path)} → {len(simplified)} 航路点")
        return simplified

    # ────────────────────────────────────────────────
    #  [F3] 动态编队旋转
    # ────────────────────────────────────────────────
    def _rotate_offsets(self, heading):
        """将局部坐标系偏移旋转至世界坐标系"""
        rot = heading - math.pi / 2  # 局部坐标 Y=前方, 对应 heading=π/2
        c, s = math.cos(rot), math.sin(rot)
        R = np.array([[c, -s, 0],
                       [s,  c, 0],
                       [0,  0, 1]])
        rotated = np.zeros_like(self.base_offsets)
        for i in range(self.num_drones):
            rotated[i] = R @ self.base_offsets[i]
        return rotated

    def _update_heading(self, target_heading):
        """
        [F3] 平滑更新编队朝向, 带角速度限制
        确保 V 型尖端始终指向前进方向, 转弯不突变
        """
        # 角度差 wrap 到 [-π, π]
        diff = target_heading - self.current_heading
        diff = (diff + math.pi) % (2 * math.pi) - math.pi

        # 角速度限制
        max_step = self.heading_rate_limit * self.dt
        diff = np.clip(diff, -max_step, max_step)

        self.current_heading += diff
        self.current_offsets = self._rotate_offsets(self.current_heading)

    # ────────────────────────────────────────────────
    #  地图加载
    # ────────────────────────────────────────────────
    def _load_map(self, path):
        blds = []
        if path and os.path.exists(path):
            with open(path) as f:
                for line in f:
                    line = line.strip()
                    if not line or line.startswith('#'):
                        continue
                    p = line.split()
                    if len(p) >= 4:
                        v = [float(x) for x in p]
                        blds.append({
                            'xmin': min(v[0], v[1]),
                            'xmax': max(v[0], v[1]),
                            'ymin': min(v[2], v[3]),
                            'ymax': max(v[2], v[3]),
                            'zmax': v[5] if len(p) >= 6 else self.default_bld_h
                        })
            print(f"🏙️  加载 {len(blds)} 栋建筑物")
        else:
            print("⚠️  无建筑物地图")
        return blds

    # ────────────────────────────────────────────────
    #  编队偏移量 (局部坐标)
    # ────────────────────────────────────────────────
    def _gen_offsets(self):
        """
        局部坐标系: Y轴=前方(编队前进方向), X轴=侧方
        V型: drone0 在原点(尖端), 其他在后方两翼展开
        """
        off = np.zeros((self.num_drones, 3))
        g = self.formation_gap
        if self.formation_type == "line":
            for i in range(self.num_drones):
                off[i] = [0, -g * i, 0]
        elif self.formation_type == "v_formation":
            for i in range(self.num_drones):
                if i == 0:
                    off[i] = [0, 0, 0]  # 尖端=领航位
                else:
                    side = 1 if i % 2 == 0 else -1
                    row = (i + 1) // 2
                    off[i] = [side * g * row, -g * row, 0]
        elif self.formation_type == "triangle":
            row = col = 0
            for i in range(self.num_drones):
                off[i] = [(col - row / 2.0) * g, -row * g, 0]
                col += 1
                if col > row:
                    row += 1; col = 0
        elif self.formation_type == "cross":
            for i in range(self.num_drones):
                if i == 0:   off[i] = [0, 0, 0]
                elif i < 5:  off[i] = [0, g * i, 0]
                elif i < 9:  off[i] = [0, -g * (i - 4), 0]
                elif i < 12: off[i] = [g * (i - 8), 0, 0]
                else:        off[i] = [-g * (i - 11), 0, 0]
        return off

    # ────────────────────────────────────────────────
    #  点到 AABB (支持 3D 计算)
    # ────────────────────────────────────────────────
    def _pt2aabb(self, pos, b):
        """
        计算点到建筑物 AABB 的最短距离向量
        返回: (距离, 推力方向, 是否在上方)
        """
        # 1. 垂直判断
        above = pos[2] > b.get('zmax', self.default_bld_h)
        
        # 2. 水平投影点 (Clamped Point)
        cx = np.clip(pos[0], b['xmin'], b['xmax'])
        cy = np.clip(pos[1], b['ymin'], b['ymax'])
        
        # 3. 水平距离向量
        dx = pos[0] - cx
        dy = pos[1] - cy
        h_dist = math.hypot(dx, dy)
        
        # 4. 判断是否在建筑内部 (h_dist ≈ 0)
        if h_dist > 1e-3:
            # 在外部 -> 方向指向外
            edir = np.array([dx / h_dist, dy / h_dist, 0.0])
            dist = h_dist
        else:
            # 在内部 -> 找最近的边推出
            # 计算到四边的距离
            d_xm = pos[0] - b['xmin']
            d_xM = b['xmax'] - pos[0]
            d_ym = pos[1] - b['ymin']
            d_yM = b['ymax'] - pos[1]
            
            min_d = min(d_xm, d_xM, d_ym, d_yM)
            dist = -min_d # 负数表示在内部
            
            if min_d == d_xm:   edir = np.array([-1.0, 0.0, 0.0])
            elif min_d == d_xM: edir = np.array([ 1.0, 0.0, 0.0])
            elif min_d == d_ym: edir = np.array([ 0.0,-1.0, 0.0])
            else:               edir = np.array([ 0.0, 1.0, 0.0])
            
        return dist, edir, above

    # ────────────────────────────────────────────────
    #  避障力 (3D 增强版)
    # ────────────────────────────────────────────────
    def _avoid_force(self, pos, vel):
        """
        计算避障力：
        - 如果上方空间足够（z < zmax + buffer），优先向上推
        - 否则水平推离
        """
        force = np.zeros(3)
        min_dist = float('inf')
        
        for b in self.buildings:
            dist, edir, above = self._pt2aabb(pos, b)
            b_zmax = b.get('zmax', self.default_bld_h)
            
            # [策略 A] 已经在建筑物上方 -> 保持高度或向上推
            if above:
                # 垂直安全余量检查
                z_margin = pos[2] - b_zmax
                # 只有当水平投影在建筑范围内（或极近），才施加垂直向上力
                # 扩展判断范围：建筑边界外 5m 也要小心
                in_range_x = (b['xmin'] - 5.0) < pos[0] < (b['xmax'] + 5.0)
                in_range_y = (b['ymin'] - 5.0) < pos[1] < (b['ymax'] + 5.0)
                
                if in_range_x and in_range_y:
                    if z_margin < 8.0: # 如果距离楼顶小于 8m，还是危险
                        push_up = 20.0 / (z_margin + 0.5) # 强力向上
                        force[2] += push_up
                continue
                
            # [策略 B] 在建筑物侧面或内部
            
            # 1. 紧急避障 (在内部或撞墙)
            if dist <= 0.5:
                # 极近距离：根据“从顶上过代价小还是绕过代价小”决定
                # 假设水平绕行半径大，而楼顶只高出一点点 -> 向上
                z_diff = b_zmax - pos[2]
                
                if z_diff < 15.0: 
                    # 楼顶就在头顶不远处 -> 优先向上飞越！
                    force[2] += self.avoid_gain * 2.0 
                    # 同时保留一点水平推力防止卡住
                    force += edir * self.avoid_gain * 0.5
                else:
                    # 楼太高了 -> 只能水平绕
                    force += edir * self.avoid_gain * 3.0
                    
            # 2. 警戒区避障 (hard_range 内)
            elif dist < self.hard_range:
                fm = self.avoid_gain / (dist + 0.1)**2
                
                # 智能决策：向上还是向外？
                z_diff = b_zmax - pos[2]
                if z_diff < 10.0:
                    # 快到楼顶高度了，尝试向上拉升
                    force[2] += fm * 0.8
                    force += edir * fm * 0.2
                else:
                    # 正常水平推离
                    force += edir * fm
                    
            # 3. 远距离感知 (detect_range)
            elif dist < self.detect_range:
                # 即使很远，如果正对着飞，也要给一点切向力
                t = (dist - self.hard_range) / (self.detect_range - self.hard_range)
                fall = (1.0 - np.clip(t, 0, 1)) ** 3
                fm = self.avoid_gain * 0.2 * fall / (dist + 1.0)
                force += edir * fm
            
            # 记录全局最近障碍物距离
            min_dist = min(min_dist, dist)

            # 4. 速度前瞻 (Lookahead)
            # 如果速度很快，要探测未来位置
            spd = np.linalg.norm(vel[:2])
            if spd > 1.0 and dist < self.detect_range:
                 la = min(self.lookahead_t, self.detect_range / spd)
                 fp = pos + vel * la # 未来位置
                 fd, fdir, fab = self._pt2aabb(fp, b)
                 
                 # 如果未来位置会撞墙 (且不在上方)
                 if not fab and fd < self.detect_range * 0.5:
                     lt = np.clip(fd / (self.detect_range * 0.5), 0, 1)
                     # 施加额外的避障力
                     force += fdir * ((1 - lt) ** 2 * 12.0)
                     
        return force, min_dist


    # ────────────────────────────────────────────────
    #  理想位置安全校正 (3D)
    # ────────────────────────────────────────────────
    def _safe_ideal(self, ideal, drone_pos):
        for b in self.buildings:
            m = 5.0
            b_zmax = b.get('zmax', self.default_bld_h)
            
            # 检查理想点是否在“建筑及其缓冲区”内部
            if (b['xmin'] - m < ideal[0] < b['xmax'] + m and
                    b['ymin'] - m < ideal[1] < b['ymax'] + m and
                    ideal[2] < b_zmax):
                
                sm = 8.0 # 把理想点推离建筑的距离
                
                # 候选位置：四面墙外 + 房顶上方
                cands = [
                    np.array([b['xmin'] - sm, ideal[1], ideal[2]]),
                    np.array([b['xmax'] + sm, ideal[1], ideal[2]]),
                    np.array([ideal[0], b['ymin'] - sm, ideal[2]]),
                    np.array([ideal[0], b['ymax'] + sm, ideal[2]]),
                    np.array([ideal[0], ideal[1], b_zmax + 5.0]), # 向上推
                ]
                
                # 选择离当前无人机位置最近的那个修正点
                # 注意：如果无人机已经在高处，这会倾向于选房顶上方
                ideal = min(cands, key=lambda c: np.linalg.norm(c - drone_pos))
        return ideal
        
    # ...existing code...

    # ────────────────────────────────────────────────
    #  [F5] 动态编队压缩 (穿窄道时收缩)
    # ────────────────────────────────────────────────
    def _update_scale(self):
        if not self.buildings:
            self.form_scale_target = 1.0
        else:
            # 从 1.0 递减, 找到能让所有无人机不碰楼的最大缩放
            for s in np.arange(1.0, 0.19, -0.05):
                ok = True
                for i in range(self.num_drones):
                    ip = self.leader_pos + self.current_offsets[i] * s
                    for b in self.buildings:
                        if (b['xmin'] - 6 < ip[0] < b['xmax'] + 6 and
                                b['ymin'] - 6 < ip[1] < b['ymax'] + 6 and
                                ip[2] < b.get('zmax', self.default_bld_h)):
                            ok = False; break
                    if not ok:
                        break
                if ok:
                    self.form_scale_target = s
                    break
            else:
                self.form_scale_target = 0.2
        # 平滑
        alpha = 0.05  # 收缩快
        if self.form_scale_target > self.form_scale:
            alpha = 0.02  # 恢复慢 (防止刚出窄道就展开太快)
        self.form_scale += (self.form_scale_target - self.form_scale) * alpha

    # ────────────────────────────────────────────────
    #  切向力重定向
    # ────────────────────────────────────────────────
    def _tangent_redirect(self, ff, avoid_dir, af_mag):
        if af_mag < 1.0 or np.linalg.norm(ff) < 0.1:
            return ff
        opposition = np.dot(ff, avoid_dir)
        if opposition < 0:
            parallel = opposition * avoid_dir
            tangent = ff - parallel
            blend = np.clip(af_mag / 10.0, 0, 1)
            return ff * (1 - blend) + tangent * blend * 1.3
        return ff

    # ────────────────────────────────────────────────
    #  自适应领航速度
    # ────────────────────────────────────────────────
    def _leader_speed(self):
        lags = [np.linalg.norm(
            self.pos[i] - (self.leader_pos +
                        self.current_offsets[i] * self.form_scale))
            for i in range(self.num_drones)]
        
        # 用 P75 分位数代替 max（容忍 25% 的掉队者）
        sorted_lags = sorted(lags)
        p75_idx = int(self.num_drones * 0.75)
        representative_lag = sorted_lags[min(p75_idx, len(sorted_lags)-1)]
        
        ml = representative_lag  # 代表性掉队距离
        
        if ml < 15:      f = 1.0
        elif ml < 40:    f = 1.0 - 0.5 * (ml - 15) / 25  # 更温和的减速
        else:            f = 0.5  # 最低 50%（而非 20%）
        
        return self.leader_base_spd * f, max(lags)  # 返回最大值用于日志

    # ────────────────────────────────────────────────
    #  停滞检测 + 逃逸
    # ────────────────────────────────────────────────
    def _check_stag(self, i, pos, ideal):
        disp = np.linalg.norm(pos - self.prev_pos[i])
        far = np.linalg.norm(pos - ideal) > 15
        slow = disp < 0.4 * self.dt
        if slow and far:
            self.stag_cnt[i] += 1
        else:
            self.stag_cnt[i] = max(0, self.stag_cnt[i] - 3)
        return self.stag_cnt[i] > int(4.0 / self.dt)

    def _escape_force(self, i, pos, ideal):
        to_ideal = ideal - pos
        d = np.linalg.norm(to_ideal[:2])
        if d < 0.1:
            return np.array([0, 0, 10.0])
        pa = np.array([-to_ideal[1], to_ideal[0], 0.0])
        pb = -pa
        n = np.linalg.norm(pa)
        if n > 0.01:
            pa /= n; pb /= n
        def probe(dr):
            tp = pos + dr * 30
            md = float('inf')
            for b in self.buildings:
                d2, _, _ = self._pt2aabb(tp, b)
                md = min(md, d2)
            return md
        best = pa if probe(pa) >= probe(pb) else pb
        self.stag_cnt[i] = 0
        return best * 12.0 + np.array([0, 0, 5.0])

    # ────────────────────────────────────────────────
    #  硬边界 (Fail-safe, 3D)
    # ────────────────────────────────────────────────
    def _hard_boundary(self, pos, vel):
        """
        强制约束：如果无人机还是不小心穿模了，强制移出。
        (优先移出最近的表面，包括房顶)
        """
        for b in self.buildings:
            b_zmax = b.get('zmax', self.default_bld_h)
            
            # 如果已经在房顶上方，则认为是安全的（忽略 simple 2D containment）
            if pos[2] >= b_zmax:
                continue
                
            # 检查是否在建筑 2D 投影内
            if (b['xmin'] < pos[0] < b['xmax'] and
                    b['ymin'] < pos[1] < b['ymax']):
                
                # 计算到各个面的距离
                d_xmin = pos[0] - b['xmin']
                d_xmax = b['xmax'] - pos[0]
                d_ymin = pos[1] - b['ymin']
                d_ymax = b['ymax'] - pos[1]
                d_zmax = b_zmax - pos[2]
                
                # 找到最近的逃逸面
                dists = [
                    (d_xmin, 0, -1), # xmin 面
                    (d_xmax, 0,  1), # xmax 面
                    (d_ymin, 1, -1), # ymin 面
                    (d_ymax, 1,  1), # ymax 面
                    (d_zmax, 2,  1)  # 房顶
                ]
                
                min_d, ax, sg = min(dists, key=lambda x: x[0])
                
                m = 0.5 # 推出的一点点距离
                
                if ax == 2:
                    # 向上推出房顶
                    pos[2] = b_zmax + m
                    if vel[2] < 0: vel[2] = 0.0
                else:
                    # 水平推出
                    if ax == 0:
                        pos[0] = (b['xmin'] - m) if sg < 0 else (b['xmax'] + m)
                    else:
                        pos[1] = (b['ymin'] - m) if sg < 0 else (b['ymax'] + m)
                    vel[ax] = 0.0 # 撞墙后该方向速度清零
                    
        return pos, vel

    # ────────────────────────────────────────────────
    #  工具
    # ────────────────────────────────────────────────
    def _gps(self, x, y, z):
        return (self.base_lat + y / 111320.0,
                self.base_lon + x / (111320.0 * math.cos(math.radians(self.base_lat))),
                self.base_alt + z)

    @staticmethod
    def _sigmoid(x, c=0.0, w=1.0):
        return 1.0 / (1.0 + math.exp(-np.clip((x - c) / max(w, 1e-6), -20, 20)))

    # ════════════════════════════════════════════════════════════
    #  主仿真循环
    # ════════════════════════════════════════════════════════════
    def generate(self, max_time=1500):
        print(f"\n🚀 v3.1 轨迹生成")
        print(f"   编队 {self.formation_type} ({self.num_drones}架)"
              f" | dt={self.dt}s | 建筑 {len(self.buildings)}栋")
        print(f"   航线 {self.start_pos} → {self.target_pos}"
              f"  ({self.mission_dist:.0f}m)\n")

        ns3 = []
        rtk = []
        t0 = datetime.now()
        steps = int(max_time / self.dt)
        log_iv = max(1, int(10 / self.dt))

        for step in range(steps):
            t = step * self.dt
            now = t0 + timedelta(seconds=t)

            # ━━ 1. 航路点跟踪 ━━
            wp = self.waypoints[self.wp_idx]
            to_wp = wp - self.leader_pos
            dw = np.linalg.norm(to_wp)
            while dw < 15 and self.wp_idx < len(self.waypoints) - 1:
                self.wp_idx += 1
                wp = self.waypoints[self.wp_idx]
                to_wp = wp - self.leader_pos
                dw = np.linalg.norm(to_wp)

            is_final = (self.wp_idx == len(self.waypoints) - 1)
            if is_final and dw < 10:
                print(f"\n🎉 集群抵达目标！耗时 {t:.1f}s")
                for i in range(self.num_drones):
                    ns3.append(f"{t:.3f},{i},{self.pos[i,0]:.3f},"
                               f"{self.pos[i,1]:.3f},{self.pos[i,2]:.3f}\n")
                break

            # ━━ 2. [F3] 更新编队朝向 → V尖始终对准下个航路点 ━━
            leader_speed = np.linalg.norm(self.leader_vel[:2])
            if leader_speed > 1.0:
                # 用实际飞行方向，自然平滑
                target_heading = math.atan2(self.leader_vel[1], self.leader_vel[0])
            else:
                # 速度太慢时保持当前朝向，避免抖动
                target_heading = self.current_heading
            self._update_heading(target_heading)

            # 当前前进方向 (编队朝向对应的单位向量)
            fwd_dir = np.array([math.cos(self.current_heading),
                                math.sin(self.current_heading), 0.0])

            # ━━ 3. 领航者更新 ━━
            spd, max_lag = self._leader_speed()
            if is_final and dw < 30:
                spd *= max(0.2, dw / 30)
            ld = to_wp / max(dw, 0.1)
            lv = ld * spd

            la_f, _ = self._avoid_force(self.leader_pos, lv)
            lv += la_f * 0.3
            ln = np.linalg.norm(lv)
            if ln > self.max_speed:
                lv = lv / ln * self.max_speed

            self.leader_pos += lv * self.dt
            self.leader_pos, lv = self._hard_boundary(self.leader_pos, lv)
            self.leader_vel = lv

            # ━━ 4. 编队缩放 ━━
            self._update_scale()

            # ━━ 5. 逐机更新 ━━
            global_min_obs = float('inf')
            self.prev_pos = self.pos.copy()

            for i in range(self.num_drones):
                p = self.pos[i].copy()
                v = self.vel[i].copy()

                # 理想位置 (用实时旋转后的偏移 × 缩放)
                ideal = (self.leader_pos +
                         self.current_offsets[i] * self.form_scale)
                ideal = self._safe_ideal(ideal, p)

                # ── A: 编队 PD 力 ──
                err = ideal - p
                ed = np.linalg.norm(err)
                kp = 1.0 if ed < 5 else (2.0 if ed < 20 else (
                    3.0 if ed < 50 else 4.5))
                ff = err * kp + (lv - v) * 0.5
                fn = np.linalg.norm(ff)
                if fn > 10:
                    ff = ff / fn * 10

                if ed > 30:  # 严重掉队
                    # 给予额外的追赶力，方向直指理想位置
                    chase_force = err / ed * min(ed * 0.3, 15.0)  # 最大 15N 追赶力
                    ff += chase_force
                    # 追赶时降低避障权重（允许走更激进的路线）
                    # 后面的 aw 会自动处理

                # ── B: 避障力 ──
                af, md = self._avoid_force(p, v)
                an = np.linalg.norm(af)
                global_min_obs = min(global_min_obs, md)

                # ── C: 切向重定向 ──
                if an > 1.0:
                    ff = self._tangent_redirect(ff, af / an, an)

                # ── D: 机间防撞 ──
                sf = np.zeros(3)
                for j in range(self.num_drones):
                    if i == j:
                        continue
                    diff = p - self.pos[j]
                    dij = np.linalg.norm(diff)
                    if dij < 0.1:
                        sf += np.random.randn(3) * 20
                    elif dij < self.sep_trigger:
                        sf += (diff / dij) * (25.0 / (dij * dij))
                    elif dij < self.formation_gap * 0.8:
                        sf += (diff / dij) * (3.0 / dij)
                sn = np.linalg.norm(sf)
                if sn > 15:
                    sf = sf / sn * 15

                # ── E: 停滞逃逸 ──
                ef = np.zeros(3)
                if self._check_stag(i, p, ideal):
                    ef = self._escape_force(i, p, ideal)

                # ── F: sigmoid 优先级混合 ──
                aw = self._sigmoid(an, 5.0, 3.0)
                blended = ff * (1 - aw * 0.95)
                total = blended + af + sf + ef

                dmax = self.max_force * (1 + 2 * aw)
                tn = np.linalg.norm(total)
                if tn > dmax:
                    total = total / tn * dmax

                # ── G: 速度积分 + 方向性阻尼 ──
                v += total * self.dt
                # 用动态朝向做阻尼分解
                fwd_comp = np.dot(v[:2], fwd_dir[:2])  # 只取 XY 前进分量
                fwd_vec = fwd_comp * fwd_dir[:2]
                lat_vec = v[:2] - fwd_vec

                # XY 平面阻尼
                v_xy = fwd_vec * 0.985 + lat_vec * 0.93

                # Z 轴单独轻阻尼（保持垂直机动能力）
                v_z = v[2] * 0.97  # 只衰减 3%，而非 7%

                v = np.array([v_xy[0], v_xy[1], v_z])

                vn = np.linalg.norm(v)
                if vn > self.max_speed:
                    v = v / vn * self.max_speed

                # ── H: 位置积分 ──
                p += v * self.dt
                if p[2] < 5:
                    p[2] = 5; v[2] = max(0, v[2])
                p, v = self._hard_boundary(p, v)

                self.pos[i] = p
                self.vel[i] = v

                # 数据输出
                ns3.append(f"{t:.3f},{i},{p[0]:.3f},{p[1]:.3f},{p[2]:.3f}\n")
                lat_, lon_, alt_ = self._gps(p[0], p[1], p[2])
                rtk.append({
                    'timestamp': now.isoformat(),
                    'drone_id': i,
                    'latitude': lat_ + np.random.normal(0, 5e-6),
                    'longitude': lon_ + np.random.normal(0, 5e-6),
                    'altitude': alt_ + np.random.normal(0, 0.05),
                    'time_sec': t,
                })

            if step and step % log_iv == 0:
                remain = np.linalg.norm(self.target_pos - self.leader_pos)
                hdg_deg = math.degrees(self.current_heading)
                print(f"  t={t:7.1f}s | 剩余{remain:7.1f}m | "
                      f"航向{hdg_deg:6.1f}° | "
                      f"缩放{self.form_scale:.2f} | "
                      f"掉队{max_lag:5.1f}m | "
                      f"近障{global_min_obs:6.1f}m | "
                      f"WP {self.wp_idx}/{len(self.waypoints)-1}")

        return ns3, pd.DataFrame(rtk)


# ════════════════════════════════════════════════════════════════
if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='无人机集群轨迹规划 v3.1')
    parser.add_argument('--num_drones', type=int, default=15)
    parser.add_argument('--formation', type=str, default='v_formation',
                        choices=['v_formation', 'line', 'cross', 'triangle'])
    parser.add_argument('--start', type=str, default='0,0,30')
    parser.add_argument('--target', type=str, default='0,500,30')
    parser.add_argument('--map', type=str, default='../data_map/custom_city.txt')
    parser.add_argument('--output', type=str,
                        default='../data_rtk/mobility_trace_custom.txt')
    parser.add_argument('--dt', type=float, default=0.1)
    parser.add_argument('--spacing', type=float, default=12.0)

    args = parser.parse_args()
    script_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(script_dir)

    sp = tuple(map(float, args.start.split(',')))
    tp = tuple(map(float, args.target.split(',')))

    print("=" * 65)
    print(f" 🛠️  v3.1 路径规划引擎 | {args.num_drones} 架")
    print("=" * 65)

    swarm = AdvancedDroneSwarm(
        num_drones=args.num_drones,
        start_pos=sp, target_pos=tp,
        formation=args.formation,
        spacing=args.spacing,
        map_file=args.map, dt=args.dt,
    )

    ns3_lines, rtk_df = swarm.generate()

    out_dir = os.path.dirname(args.output)
    if out_dir and not os.path.exists(out_dir):
        os.makedirs(out_dir)

    with open(args.output, 'w') as f:
        f.write("# ==========================================\n")
        f.write(f"# v3.1 轨迹 | {args.formation} | dt={args.dt}\n")
        f.write(f"# {args.start} -> {args.target}\n")
        f.write("# time(s),nodeId,x(m),y(m),z(m)\n")
        f.write("# ==========================================\n")
        f.writelines(ns3_lines)

    print(f"\n✅ 轨迹已导出: {args.output} ({len(ns3_lines)} 条)")