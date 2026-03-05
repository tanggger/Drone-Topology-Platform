# RTK航迹数据对齐算法可视化

本目录包含了RTK航迹数据到通信仿真数据对齐算法的完整可视化结果。

## 生成的可视化文件

### 静态图片
- `time_alignment.png` - 时间对齐效果图
- `space_alignment.png` - 空间对齐效果图（GPS → ENU转换）
- `alignment_summary.png` - 对齐效果综合总结图

### 动画文件
- `progress_animation.gif` - 时间对齐后的动画式进度条
- `trajectory_smoothing.gif` - 轨迹插值与平滑的动态对比

### 报告文件
- `visualization_report.html` - 完整的可视化报告（推荐在浏览器中打开）
- `visualize_alignment.py` - 生成所有可视化的Python脚本

## 如何查看

### 方法1：浏览器查看（推荐）
```bash
# 在浏览器中打开HTML报告
firefox visualization_report.html
# 或
google-chrome visualization_report.html
```

### 方法2：直接查看图片
```bash
# 查看静态图片
eog *.png

# 查看动画（需要支持GIF的查看器）
eog *.gif
```

## PPT制作建议

### 时间对齐部分
- 使用 `time_alignment.png` 展示时间轴对照效果
- 使用 `progress_animation.gif` 作为动态演示
- 重点说明：统一时间基准、支持时间缩放

### 空间对齐部分  
- 使用 `space_alignment.png` 展示GPS→ENU转换
- 突出显示：坐标系转换、空间关系保持
- 可以截取左右两个子图分别展示

### 轨迹平滑部分
- 使用 `trajectory_smoothing.gif` 作为核心动画
- 对比展示：原始噪声 vs 平滑轨迹
- 强调：插值补点、噪声消除

### 综合效果
- 使用 `alignment_summary.png` 展示整体效果
- 四个象限分别说明不同维度的对齐结果

## 数据统计

- **无人机数量**: 20架
- **仿真时长**: 100秒  
- **数据点总数**: 10,000个
- **时间精度**: 0.2秒
- **空间范围**: X: -34.3~1344.1米, Y: -172.4~0.3米, Z: -24.3~25.5米

## 重新生成可视化

如果需要重新生成或修改可视化，可以运行：

```bash
python3 visualize_alignment.py
```

脚本会自动读取当前目录下的数据文件并生成所有可视化结果。

## 技术特点

1. **自动化处理**: 一键生成所有可视化结果
2. **多维度展示**: 时间、空间、轨迹三个维度全覆盖  
3. **动静结合**: 静态图表 + 动画演示
4. **专业美观**: 适合学术汇报和技术展示
5. **数据驱动**: 基于真实仿真数据生成