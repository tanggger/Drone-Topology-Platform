# 🚀 Wing-Net Omni 数字孪生平台：前后端 API 联调与对接指南

本文档专为**前端开发人员（或前端 AI 助手）**编写。
后端系统（基于高频物理引擎 NS-3 和动态图着色算法）已经通过 Flask 封装为标准的 RESTful API。这里包含了从启动推演、获取计算结果到前端 3D 渲染对接的所有必备知识和代码示例。

---

## 1. 架构概述

整个平台的交互采用**异步轮询任务机制**，原因在于后端的 NS-3 C++ 核心执行完整的无人机集群电磁对抗仿真需要耗费一定的计算时间（通常在 10秒 ~ 1分钟之间，取决于集群规模 `num_drones`）。

*   **API 基准地址**: `http://<后端机器IP或localhost>:5000`
*   **通信数据格式**: `application/json`
*   **CORS 跨域**: 后端已全量开启 CORS，前端可直接在任何端口/域名下发起本地 AJAX/Fetch 请求。

---

## 2. API 接口文档

### 2.1 健康检查 (Health Check)
用于检测后端服务器是否存活。
*   **Method:** `GET`
*   **Endpoint:** `/api/health`
*   **Response (200 OK):**
    ```json
    {
      "status": "OK",
      "message": "Wing-Net Omni Backend Server is running."
    }
    ```

### 2.2 发起仿真推演任务 (Trigger Simulation)
向后端提交大屏上配置好的各项参数，触发底层物理沙盘引擎。
*   **Method:** `POST`
*   **Endpoint:** `/api/simulate`
*   **Request Body:**
    ```json
    {
      "num_drones": 30,                 // 飞机数量
      "formation": "v_formation",       // 编队类型: v_formation, line, cross, triangle, custom
      "difficulty": "Hard",             // 干扰与地理难度: Easy, Moderate, Hard
      "strategy": "dynamic",            // AI策略: static (基线), dynamic (图着色自适应)
      "start": "0,0,30",                // 群体默认起飞点坐标 (x,y,z)
      "target": "0,600,30",             // 群体默认目标点坐标 (x,y,z)
      "buildings": [                    // 前端交互时框选生成的城市大楼阻隔物
        {
          "xMin": 50, "xMax": 90, 
          "yMin": 200, "yMax": 240, 
          "zMin": 0, "zMax": 300
        }
      ]
    }
    ```
*   **Response (200 OK):**
    ```json
    {
      "message": "Simulation triggered successfully",
      "task_id": "a1b2c3d4",            // ⭐️ 重要：拿到这个排队号，用于轮询结果
      "status": "RUNNING"
    }
    ```

### 2.3 轮询获取任务结果 (Poll Results)
前端携带上一步拿到的 `task_id`，每隔一定时间（建议 3~5 秒）来叩门询问。
*   **Method:** `GET`
*   **Endpoint:** `/api/results/<task_id>`
*   **Response:**
    **A. 如果还在算力推演中：**
    ```json
    { "status": "RUNNING" }
    ```
    **B. 如果计算失败（炸机了）：**
    ```json
    { "status": "FAILED", "error": "具体的 Python 或 C++ 报错堆栈" }
    ```
    **C. 如果计算成功出结果（大满贯）：**
    ```json
    {
      "status": "SUCCESS",
      "data": {
        // [动画驱动层] 实时位置（含RTK抖动与避障机动绕飞点位）
        "positions": [
          {"time": 0.0, "nodeId": 0, "x": 0.0, "y": 0.0, "z": 30.0},
          ...
        ],
        // [动画驱动层] 通信激光特效 (采样数据)
        "transmissions": [
          {"time": 1.25, "nodeId": 3, "eventType": "Tx Data"},
          ...
        ],
        // [动画驱动层] 实时组网连线状态 (每2秒更新一次快照)
        "topology_links": [
          "0.0-2.0s: Node0-Node1, Node0-Node2, Node4-Node5",
          ...
        ],
        // [指标层] 拓扑与连通性概览演化
        "topology_evolution": [
          {"time": 0.0, "num_links": 435, "connectivity": 1.0},
          ...
        ],
        // [指标层] 最终结算面板汇总统计
        "flow_summary": [
          {"FlowId": 1, "Src": 0, "Dest": 7, "Tx": 100, "Rx": 95, "LossRate": "5.0%"},
          ...
        ],
        // [指标层] PDR（包到达率）/ 时延 / 吞吐量性能演进
        "qos": [
          {"time": 0.0, "uav0_pdr": 0.99, "uav0_delay": 0.012, "uav0_throughput": 4500.5},
          ...
        ]
      }
    }
    ```

---

## 3. 标准的前端 Axios 对接代码范例

你可以直接把以下代码放进前端的大屏 `[开始推演]` 按钮的 onClick 方法中：

```javascript
import axios from 'axios';

// 假设我们后端的IP目前是本地
const BASE_URL = 'http://localhost:5000';

async function runTwinSimulation(uiConfig) {
  try {
    console.log("1. 提交推演参数...");
    // 1. 发射配置给后端
    const response = await axios.post(`${BASE_URL}/api/simulate`, uiConfig);
    const taskId = response.data.task_id;
    console.log(`仿真已在云端启动，任务通道舱 ID: ${taskId}`);
    
    // 2. 开始轮询结果，每隔 5 秒查一次
    console.log("2. 正在等待 NS-3 集群算力执行...");
    const timer = setInterval(async () => {
      const resultRes = await axios.get(`${BASE_URL}/api/results/${taskId}`);
      const status = resultRes.data.status;

      if (status === 'SUCCESS') {
        clearInterval(timer); // 停止轮询
        console.log("3. 🎉 数据结算完成！获取推演引擎返回的高研级大礼包！");
        
        const engineData = resultRes.data.data;
        
        // ---- ✨ 交给 3D 和 Three.js / ECharts 的驱动渲染时刻 ✨ ----
        
        // (1) 把它交给 Three.js 驱动 3D 飞机模型按照时间线飞
        const positions = engineData.positions; 
        
        // (2) 渲染 3D 连线快照 (解析 "Node0-Node1" 字符串并在两机间连线)
        const linksSnapshot = engineData.topology_links;
        
        // (3) 触发通信激光动画
        const lasers = engineData.transmissions; 
        
        // (4) 把它交给右侧 ECharts 渲染连通率图表
        const topology = engineData.topology_evolution;
        
        // (5) 把它交给底部 PDR 雷达/折线图
        const qos = engineData.qos;
        
        // (6) 仿真结束后的总结算面板
        const summary = engineData.flow_summary;

      } else if (status === 'FAILED') {
        clearInterval(timer);
        console.error("❌ 后端引擎推演崩溃了：", resultRes.data.error);
        alert("服务器推演发生故障，请检查控制台堆栈。");
      } else {
        console.log("   (后端算力引擎正在轰鸣中...请稍候)");
      }
    }, 5000);

  } catch (error) {
    console.error("请求后端 API 失败，是不是服务器没启动？", error);
  }
}

// 执行样例
// runTwinSimulation({
//    num_drones: 30, formation: "v_formation", difficulty: "Hard", strategy: "dynamic", buildings: []
// });
```

---

## 4. ⚠️ 前端开发对接避坑指南（重大提示）

1. **坐标系对齐问题**：
   * 后端 NS-3 使用的是标准 **右手笛卡尔绝对物理坐标系 (米, meter)**。
   * 如果前端大屏使用的是地图级别坐标（如 Cesium 的经纬度 Long/Lat），前端需要在拿到 `data.positions` 的 `x, y, z` 以后，**自行写一个坐标系转换矩阵（Offset/Scale）**，把后端的局部网格坐标（如 `x=500, y=600`）映射到北京海淀或深圳南山的真实经纬度上。
2. **时间轴同步**：
   * 后端吐出的是离散事件散点，`time` 字段是以 `0.5秒` 甚至更小为刻度的。
   * 引擎并不提供逐帧如 60FPS 的细腻补间，请前端 3D 开发人员在拿到每一秒的 Keyframe（关键帧航点）后，**利用 Three.js / GOSAP 的插值动画 (Interpolation/Tween)**，完成两点之间极其丝滑的平移和过渡。
3. **响应等待与前端交互**：
   * 必须在 UI 界面给用户做一个**超大且酷炫的 Loading 动效**（比如：“正在连接高算力集群...物理建模引擎初始化中...”），因为 50 架飞机在 Hard 加上复杂障碍物的算力开销很容易让人以为网页卡死了。
4. **数据的列名 (Keys)**：
   * QoS 数组中，如果前端传入的是 30 架飞机，这里面的 key 就是 `uav0_pdr`, `uav1_pdr` ... 一直编排到 `uav29_pdr`。请务必使用动态的 `Object.keys()` 循环来提取图表数据，而不要硬编码写死变量名！
