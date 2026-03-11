import xml.etree.ElementTree as ET
import math
import json
import os
import sys
import random

def convert_osm_to_simulation_map(osm_file_path, output_txt, output_json):
    """
    将地图文件 (OSM XML, OSM JSON, GeoJSON) 解析为 NS-3 需要的 .txt 建筑物文件和前端需要的三维 .json 文件
    """
    print(f"正在读取地图文件: {osm_file_path}")

    # ================= 数据加载与标准化 =================
    raw_buildings = [] # list of {'lats': [], 'lons': [], 'tags': {}}
    
    # 辅助：从 tags 提取高度
    def parse_height_from_tags(tags):
        # 默认高度策略
        default_levels = random.randint(3, 12) 
        levels = default_levels 
        height = None
        
        # 检查是否是建筑
        is_build = False
        if 'building' in tags and tags['building'] != 'no':
            is_build = True
            v = tags['building']
            # 类型特化
            if v in ['dormitory', 'apartments', 'office', 'university', 'commercial']:
                 levels = max(levels, random.randint(8, 20))
            if v in ['skyscraper']:
                 levels = random.randint(30, 60)
        
        if not is_build:
             # 如果没有 building tag，但有 height 也算
             if 'height' in tags or 'building:levels' in tags:
                 is_build = True
        
        if not is_build:
            return False, 0.0

        if 'building:levels' in tags:
            try: levels = float(tags['building:levels'])
            except: pass
        
        if 'height' in tags:
            try: 
                 v = tags['height']
                 clean_v = ''.join(c for c in v if c.isdigit() or c == '.')
                 if clean_v: height = float(clean_v)
            except: pass
            
        final_height = round(height if height is not None else levels * 3.5, 2)
        return True, final_height

    # --- Case 1: XML / OSM ---
    if osm_file_path.endswith('.osm') or osm_file_path.endswith('.xml'):
        tree = ET.parse(osm_file_path)
        root = tree.getroot()

        nodes = {}
        for node in root.findall('node'):
            nodes[node.attrib['id']] = (float(node.attrib['lat']), float(node.attrib['lon']))

        def get_way_nodes_xml(way_elem):
            lats, lons = [], []
            for nd in way_elem.findall('nd'):
                ref = nd.attrib['ref']
                if ref in nodes:
                    lats.append(nodes[ref][0]); lons.append(nodes[ref][1])
            return lats, lons
            
        def get_tags_xml(elem):
            return {t.attrib['k']: t.attrib['v'] for t in elem.findall('tag')}

        # Extract Ways
        for way in root.findall('way'):
            tags = get_tags_xml(way)
            is_b, h = parse_height_from_tags(tags)
            if is_b:
                lats, lons = get_way_nodes_xml(way)
                if lats:
                    raw_buildings.append({'lats': lats, 'lons': lons, 'height': h})

        # Extract Relations (simplified)
        for rel in root.findall('relation'):
            tags = get_tags_xml(rel)
            is_b, h = parse_height_from_tags(tags)
            if is_b:
                for member in rel.findall('member'):
                    if member.attrib.get('type') == 'way':
                        way_ref = member.attrib['ref']
                        # Find matching way (slow scan)
                        for w in root.findall('way'):
                            if w.attrib.get('id') == way_ref:
                                lats, lons = get_way_nodes_xml(w)
                                if lats:
                                    raw_buildings.append({'lats': lats, 'lons': lons, 'height': h})
                                break

    # --- Case 2: JSON (OSM JSON or GeoJSON) ---
    elif osm_file_path.endswith('.json') or osm_file_path.endswith('.geojson'):
        with open(osm_file_path, 'r', encoding='utf-8') as f:
            data = json.load(f)
            
        # (A) OSM JSON Format (Overpass)
        if 'elements' in data:
            nodes = {}
            # Pass 1: Nodes
            for el in data['elements']:
                if el['type'] == 'node':
                    nodes[el['id']] = (el['lat'], el['lon'])
            
            # Pass 2: Ways
            for el in data['elements']:
                if el['type'] == 'way':
                    tags = el.get('tags', {})
                    is_b, h = parse_height_from_tags(tags)
                    if is_b and 'nodes' in el:
                        lats = [nodes[nid][0] for nid in el['nodes'] if nid in nodes]
                        lons = [nodes[nid][1] for nid in el['nodes'] if nid in nodes]
                        if lats:
                            raw_buildings.append({'lats': lats, 'lons': lons, 'height': h})
                            
        # (B) GeoJSON Format
        elif 'features' in data:
            for feat in data['features']:
                props = feat.get('properties', {})
                # GeoJSON often puts tags in properties directly
                # If "building" key missing, but file is "export.json", assume all polygons are features?
                # Better safe: check building tag or assume yes if it's a Polygon
                tags = props # Use properties as tags
                geom = feat.get('geometry', {})
                
                # Check height tag or force if it looks like a building
                if 'building' not in tags:
                     # Try to infer
                     if geom.get('type') in ['Polygon', 'MultiPolygon']:
                         tags['building'] = 'yes' # Force treat as building
                
                is_b, h = parse_height_from_tags(tags)
                if is_b and geom:
                    gtype = geom['type']
                    coords = geom['coordinates']
                    
                    poly_coord_lists = []
                    if gtype == 'Polygon':
                        poly_coord_lists.append(coords[0]) # Outer ring
                    elif gtype == 'MultiPolygon':
                        for poly in coords:
                            poly_coord_lists.append(poly[0])
                    
                    for ring in poly_coord_lists:
                        # GeoJSON is [lon, lat]
                        lons = [p[0] for p in ring]
                        lats = [p[1] for p in ring]
                        if lats:
                            raw_buildings.append({'lats': lats, 'lons': lons, 'height': h})

    else:
        print("❌ 未知文件格式 (支持 .osm, .xml, .json, .geojson)")
        return False, 0, 0

    # ================= 坐标投影与转换 (标准化逻辑) =================
    
    # 提取所有点用于计算中心
    all_lats, all_lons = [], []
    for b in raw_buildings:
        all_lats.extend(b['lats'])
        all_lons.extend(b['lons'])

    if not all_lats:
        print("⚠️ 未找到任何建筑物数据！")
        return False, 0, 0

    # 2. 确定中心点
    center_lat = (min(all_lats) + max(all_lats)) / 2.0
    center_lon = (min(all_lons) + max(all_lons)) / 2.0
    print(f"🌍 地图中心: Lat {center_lat:.6f}, Lon {center_lon:.6f}")

    # 3. 投影函数
    def latlon_to_meters(lat, lon):
        R = 6378137.0
        y = (lat - center_lat) * (math.pi / 180.0) * R
        x = (lon - center_lon) * (math.pi / 180.0) * R * math.cos(math.radians(center_lat))
        return x, y

    # 4. 生成最终建筑物列表 (含几何信息)
    buildings = []
    for rb in raw_buildings:
        lats, lons, h = rb['lats'], rb['lons'], rb['height']
        xs, ys = [], []
        for la, lo in zip(lats, lons):
            x, y = latlon_to_meters(la, lo)
            xs.append(x); ys.append(y)
        
        # AABB
        if not xs: continue
        
        # 简化 Polygon 点集 (保留2位小数)
        poly = [{"x": round(x, 2), "y": round(y, 2)} for x, y in zip(xs, ys)]
        
        buildings.append({
            "xMin": round(min(xs), 2), "xMax": round(max(xs), 2),
            "yMin": round(min(ys), 2), "yMax": round(max(ys), 2),
            "zMin": 0.0,
            "zMax": h,
            "polygon": poly
        })

    # (此后逻辑相同: 计算偏移，写入文件)

    # 计算生成出地图沙盘的总大小（返回包围盒对角线长度供前端适配缩放）
    if buildings:
        all_x = [b['xMin'] for b in buildings] + [b['xMax'] for b in buildings]
        all_y = [b['yMin'] for b in buildings] + [b['yMax'] for b in buildings]
        map_width = max(all_x) - min(all_x)
        map_height = max(all_y) - min(all_y)
        
        # 将所有的建筑坐标做绝对偏移，确保整个地图的正中心落在 (W/2, H/2)，且没有负数坐标
        offset_x = map_width / 2.0 - ((max(all_x) + min(all_x)) / 2.0)
        offset_y = map_height / 2.0 - ((max(all_y) + min(all_y)) / 2.0)
        
        for b in buildings:
            b['xMin'] = round(b['xMin'] + offset_x, 2)
            b['xMax'] = round(b['xMax'] + offset_x, 2)
            b['yMin'] = round(b['yMin'] + offset_y, 2)
            b['yMax'] = round(b['yMax'] + offset_y, 2)
            
            # 同时将多面体顶点坐标也偏移
            if 'polygon' in b:
                for pt in b['polygon']:
                    pt['x'] = round(pt['x'] + offset_x, 2)
                    pt['y'] = round(pt['y'] + offset_y, 2)
            if 'polygons' in b:
                for poly in b['polygons']:
                    for pt in poly:
                        pt['x'] = round(pt['x'] + offset_x, 2)
                        pt['y'] = round(pt['y'] + offset_y, 2)
    else:
        map_width = 300
        map_height = 300
                
    # ============= 输出文件 =============
    # 安全创建目录
    def safe_makedirs(file_path):
        d = os.path.dirname(file_path)
        if d: os.makedirs(d, exist_ok=True)

    # 产物 A: 给底层 NS-3 (等同于目前的 data_map/xxxx.txt 格式)
    safe_makedirs(output_txt)
    with open(output_txt, 'w') as f:
        f.write("# xMin xMax yMin yMax zMin zMax\n")
        f.write(f"# 地图自动生成. 沙盘尺寸: {map_width:.1f}x{map_height:.1f} 米\n")
        for b in buildings:
            # 加入一点点偏移，防止 xMin == xMax 的薄片建筑导致 NS3 几何引擎错误
            x_max_val = b['xMax'] if b['xMax'] > b['xMin'] else b['xMin'] + 0.1
            y_max_val = b['yMax'] if b['yMax'] > b['yMin'] else b['yMin'] + 0.1
            f.write(f"{b['xMin']} {x_max_val} {b['yMin']} {y_max_val} {b['zMin']} {b['zMax']}\n")
            
    # 产物 B: 给大屏前端渲染的配置 JSON 数据
    safe_makedirs(output_json)
    with open(output_json, 'w') as f:
        output_data = {
            "map_width": round(map_width, 2),
            "map_height": round(map_height, 2),
            "buildings": buildings
        }
        json.dump(output_data, f, indent=2)

    print(f"成功解析了 {len(buildings)} 栋建筑！")
    print(f"物理碰撞地图保存至: {output_txt}")
    print(f"数字孪生渲染文件保存至: {output_json}")
    return True, map_width, map_height

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 osm_to_simulation.py <input.osm> <map_name>")
        sys.exit(1)
        
    input_osm = sys.argv[1]
    map_name = sys.argv[2]
    
    # 获取工程根目录
    try:
        ns3_dir = os.path.dirname(os.path.abspath(__file__))
    except NameError:
        ns3_dir = os.getcwd()
        
    output_txt = os.path.join(ns3_dir, f"data_map/city_{map_name}.txt")
    output_json = os.path.join(ns3_dir, f"api_server/static/{map_name}_buildings.json")
    
    convert_osm_to_simulation_map(input_osm, output_txt, output_json)
