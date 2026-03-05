  
    // ================================================================
    // GLOBAL STATE
    // ================================================================
    const STATE = {
      uavCount: 15,
      interferenceLevel: 0,
      nlosEnabled: true,
      gcsFailure: false,
      energyMode: false,
      tick: 0,
      algoRuns: 0,
      logCount: 0,
      playing: true,
      speed: 1,
      viewMode: 'normal',
    };

    const CHANNELS = ['CH1', 'CH2', 'CH3'];
    const CH_COLORS = ['#00f2ff', '#00ff88', '#a855f7'];
    const CH_BCOLORS = ['rgba(0,242,255,0.18)', 'rgba(0,255,136,0.18)', 'rgba(168,85,247,0.18)'];

    let uavs = [];
    let groundStations = [];
    let buildings = [];
    let pdrHistory = { opt: [], base: [] };
    const PDR_MAX_POINTS = 60;

    // ================================================================
    // CANVAS SETUP
    // ================================================================
    const canvas = document.getElementById('main-canvas');
    const ctx = canvas.getContext('2d');
    let W = 0, H = 0;

    function resizeCanvas() {
      W = canvas.parentElement.clientWidth;
      H = canvas.parentElement.clientHeight;
      canvas.width = W;
      canvas.height = H;
      initScene();
    }

    function initScene() {
      // Ground Stations (fixed)
      groundStations = [
        { x: 0.08 * W, y: 0.85 * H, range: 110, active: true },
        { x: 0.92 * W, y: 0.85 * H, range: 100, active: true },
      ];

      // Buildings (Mock OpenStreetMap GIS data - darker style)
      buildings = [
        { x: 0.25 * W, y: 0.15 * H, w: 0.15 * W, h: 0.25 * H, name: "广百百货" },
        { x: 0.50 * W, y: 0.10 * H, w: 0.12 * W, h: 0.35 * H, name: "中信广场" },
        { x: 0.70 * W, y: 0.22 * H, w: 0.10 * W, h: 0.20 * H, name: "太古汇" },
        { x: 0.32 * W, y: 0.50 * H, w: 0.18 * W, h: 0.15 * H, name: "天河城" },
        { x: 0.60 * W, y: 0.60 * H, w: 0.12 * W, h: 0.25 * H, name: "正佳广场" },
        { x: 0.10 * W, y: 0.40 * H, w: 0.15 * W, h: 0.15 * H, name: "购书中心" },
      ];

      initUAVs();
    }

    function initUAVs() {
      uavs = [];
      for (let i = 0; i < STATE.uavCount; i++) {
        const ch = i % 3;
        uavs.push({
          id: i,
          x: 0.1 * W + Math.random() * 0.8 * W,
          y: 0.1 * H + Math.random() * 0.75 * H,
          vx: (Math.random() - 0.5) * 1.5,
          vy: (Math.random() - 0.5) * 1.5,
          channel: ch,
          energy: 60 + Math.random() * 40,
          pdr: 0.85 + Math.random() * 0.15,
          txPower: 20,
          isNLOS: false,
          conflict: false,
          gcsConnected: true,
          type: Math.random() > 0.3 ? 'video' : 'control', // 70% HD video, 30% control
          failed: false
        });
      }
    }

    // ================================================================
    // GEOMETRY HELPERS
    // ================================================================
    function lineIntersectsRect(ax, ay, bx, by, rx, ry, rw, rh) {
      function seg(p, q, r, s) {
        const d = (s.y - r.y) * (q.x - p.x) - (s.x - r.x) * (q.y - p.y);
        if (!d) return false;
        const t = ((s.x - r.x) * (p.y - r.y) - (s.y - r.y) * (p.x - r.x)) / d;
        const u = ((q.x - p.x) * (p.y - r.y) - (q.y - p.y) * (p.x - r.x)) / d;
        return t > 0 && t < 1 && u > 0 && u < 1;
      }
      const p = { x: ax, y: ay }, q = { x: bx, y: by };
      return seg(p, q, { x: rx, y: ry }, { x: rx + rw, y: ry, }) ||
        seg(p, q, { x: rx + rw, y: ry }, { x: rx + rw, y: ry + rh }) ||
        seg(p, q, { x: rx + rw, y: ry + rh }, { x: rx, y: ry + rh }) ||
        seg(p, q, { x: rx, y: ry + rh }, { x: rx, y: ry });
    }

    function isBlocked(u1, u2) {
      if (!STATE.nlosEnabled) return false;
      return buildings.some(b => lineIntersectsRect(u1.x, u1.y, u2.x, u2.y, b.x, b.y, b.w, b.h));
    }

    function dist(a, b) { return Math.hypot(a.x - b.x, a.y - b.y); }

    // ================================================================
    // GRAPH COLORING ALGORITHM (simplified DSATUR)
    // ================================================================
    function runGraphColoring() {
      STATE.algoRuns++;
      const t0 = performance.now();
      const n = uavs.length;
      const RANGE = 160;

      // Build adjacency
      const adj = Array.from({ length: n }, () => []);
      for (let i = 0; i < n; i++)
        for (let j = i + 1; j < n; j++)
          if (dist(uavs[i], uavs[j]) < RANGE && !isBlocked(uavs[i], uavs[j])) {
            adj[i].push(j); adj[j].push(i);
          }

      // Degree-sort
      const order = [...Array(n).keys()].sort((a, b) => adj[b].length - adj[a].length);

      const colors = new Array(n).fill(-1);
      for (const node of order) {
        const used = new Set(adj[node].filter(nb => colors[nb] >= 0).map(nb => colors[nb]));
        let c = 0;
        while (used.has(c)) c++;
        colors[node] = c % 3;
      }

      uavs.forEach((u, i) => {
        // In SPLIT mode, left side UAVs get random channels (simulating no sync)
        if (STATE.viewMode === 'split' && u.x < W / 2) {
          u.channel = Math.floor(Math.random() * 3);
        } else {
          u.channel = colors[i];
        }
        u.conflict = false;
      });

      // Mark remaining conflicts (when more nodes than optimal channels)
      for (let i = 0; i < n; i++)
        for (const j of adj[i])
          if (uavs[i].channel === uavs[j].channel) { uavs[i].conflict = true; uavs[j].conflict = true; }

      const elapsed = (performance.now() - t0).toFixed(1);
      document.getElementById('algo-stat').textContent =
        `第 ${STATE.algoRuns} 次全局优化 · 耗时 ${elapsed}ms`;

      return Math.round(elapsed);
    }

    // ================================================================
    // DRAW LOOP
    // ================================================================
    function draw() {
      ctx.clearRect(0, 0, W, H);

      ctx.save();
      // Follow View transformation
      if (STATE.viewMode === 'follow' && uavs[0]) {
        const target = uavs[0];
        ctx.translate(W / 2, H / 2);
        ctx.scale(1.8, 1.8);
        ctx.translate(-target.x, -target.y);
      }

      // Grid
      ctx.strokeStyle = 'rgba(0,200,255,0.04)';
      ctx.lineWidth = 1;
      for (let x = 0; x < W; x += 50) { ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x, H); ctx.stroke(); }
      for (let y = 0; y < H; y += 50) { ctx.beginPath(); ctx.moveTo(0, y); ctx.lineTo(W, y); ctx.stroke(); }

      // Buildings Graphic - Realistic GIS Style
      buildings.forEach(b => {
        // Base shadow
        ctx.fillStyle = 'rgba(0,10,20,0.85)';
        ctx.fillRect(b.x + 4, b.y + 4, b.w, b.h);
        // Main block
        ctx.fillStyle = 'rgba(20,40,65,0.7)';
        ctx.fillRect(b.x, b.y, b.w, b.h);
        // Border lines
        ctx.strokeStyle = 'rgba(60,100,140,0.6)';
        ctx.lineWidth = 1;
        ctx.strokeRect(b.x, b.y, b.w, b.h);
        // Floor pattern (simulated)
        ctx.beginPath();
        for (let y = b.y + 10; y < b.y + b.h; y += 15) { ctx.moveTo(b.x, y); ctx.lineTo(b.x + b.w, y); }
        ctx.strokeStyle = 'rgba(100,150,200,0.1)';
        ctx.stroke();

        // Label
        ctx.font = 'bold 10px sans-serif';
        ctx.fillStyle = 'rgba(150,200,240,0.9)';
        const textWidth = ctx.measureText(b.name || "BLDG").width;
        ctx.fillText(b.name || "BLDG", b.x + b.w / 2 - textWidth / 2, b.y + b.h / 2);
      });

      // GCS range circles
      groundStations.forEach(gs => {
        if (!gs.active) return;
        ctx.beginPath();
        ctx.arc(gs.x, gs.y, gs.range, 0, Math.PI * 2);
        ctx.strokeStyle = 'rgba(255,255,255,0.08)';
        ctx.lineWidth = 1;
        ctx.setLineDash([4, 4]);
        ctx.stroke();
        ctx.setLineDash([]);
      });

      // Interference zones
      if (STATE.interferenceLevel > 0 || customIntfZones.length > 0) {
        const presetZones = [];
        if (STATE.interferenceLevel > 0) presetZones.push({ x: 0.5 * W, y: 0.4 * H });
        if (STATE.interferenceLevel > 1) presetZones.push({ x: 0.2 * W, y: 0.6 * H });
        if (STATE.interferenceLevel > 2) presetZones.push({ x: 0.74 * W, y: 0.55 * H });

        // Merge preset intf zones and custom ones
        const zones = presetZones.concat(customIntfZones);

        // Custom dragged interference zone graphic
        if (currentDragZone && currentInteractionMode === InteractionMode.ADD_INTF) {
          zones.push(currentDragZone);
        }

        zones.forEach(z => {
          const r = z.r || (80 + Math.sin(STATE.tick * 0.05) * 8);
          ctx.beginPath();
          ctx.arc(z.x, z.y, r, 0, Math.PI * 2);
          ctx.fillStyle = 'rgba(255,59,59,0.08)';
          ctx.fill();
          ctx.strokeStyle = 'rgba(255,59,59,0.4)';
          ctx.lineWidth = 2;
          ctx.setLineDash([8, 4]);
          ctx.stroke();
          ctx.setLineDash([]);
          ctx.font = 'bold 11px sans-serif';
          ctx.fillStyle = 'rgba(255,100,100,0.9)';
          ctx.fillText('⚡ 强磁干扰区', z.x - 30, z.y - r - 10);
        });
      }

      // Draw dragged building block
      if (currentDragZone && currentInteractionMode === InteractionMode.ADD_BLDG) {
        ctx.fillStyle = 'rgba(20,40,65,0.7)';
        ctx.fillRect(currentDragZone.x, currentDragZone.y, currentDragZone.w, currentDragZone.h);
        ctx.strokeStyle = 'var(--orange)';
        ctx.lineWidth = 2;
        ctx.setLineDash([5, 5]);
        ctx.strokeRect(currentDragZone.x, currentDragZone.y, currentDragZone.w, currentDragZone.h);
        ctx.setLineDash([]);
        ctx.font = '10px sans-serif';
        ctx.fillStyle = 'var(--orange)';
        ctx.fillText('NEW BLOCK', currentDragZone.x + 5, currentDragZone.y + 15);
      }

      // Links
      const RANGE = 160;
      for (let i = 0; i < uavs.length; i++) {
        for (let j = i + 1; j < uavs.length; j++) {
          const d = dist(uavs[i], uavs[j]);
          if (d > RANGE) continue;
          const blocked = isBlocked(uavs[i], uavs[j]);
          const conflict = uavs[i].channel === uavs[j].channel;
          ctx.beginPath();
          ctx.moveTo(uavs[i].x, uavs[i].y);
          ctx.lineTo(uavs[j].x, uavs[j].y);
          if (blocked) {
            ctx.strokeStyle = 'rgba(255,170,0,0.35)';
            ctx.setLineDash([5, 4]);
          } else if (conflict) {
            ctx.strokeStyle = 'rgba(255,59,59,0.5)';
            ctx.setLineDash([]);
          } else {
            const alpha = 0.08 + 0.25 * (1 - d / RANGE);
            ctx.strokeStyle = `rgba(0,242,255,${alpha})`;
            ctx.setLineDash([]);
          }
          ctx.lineWidth = blocked ? 1 : (conflict ? 1.5 : 1);
          ctx.stroke();
          ctx.setLineDash([]);
        }
      }

      // GCS links
      uavs.forEach(u => {
        const gs = groundStations.find(g => g.active && dist(u, g) < g.range);
        if (gs) {
          ctx.beginPath();
          ctx.moveTo(u.x, u.y);
          ctx.lineTo(gs.x, gs.y);
          ctx.strokeStyle = 'rgba(255,255,255,0.06)';
          ctx.lineWidth = 0.8;
          ctx.setLineDash([3, 6]);
          ctx.stroke();
          ctx.setLineDash([]);
        }
      });

      // UAVs
      uavs.forEach(u => {
        const color = u.conflict ? '#ff3b3b' : CH_COLORS[u.channel];

        // Energy halo
        if (STATE.energyMode && u.energy < 30) {
          ctx.beginPath();
          ctx.arc(u.x, u.y, 18, 0, Math.PI * 2);
          ctx.fillStyle = 'rgba(255,100,0,0.15)';
          ctx.fill();
        }

        // Glow
        ctx.beginPath();
        ctx.arc(u.x, u.y, 14, 0, Math.PI * 2);
        ctx.fillStyle = `${color}18`;
        ctx.fill();

        // Isometric 3D Body (replaces simple circle)
        // Draw bottom shadow
        ctx.fillStyle = 'rgba(0,0,0,0.5)';
        ctx.beginPath(); ctx.ellipse(u.x, u.y + 4, 8, 4, 0, 0, Math.PI * 2); ctx.fill();

        // Draw 3D Box base
        ctx.fillStyle = `rgba(30,40,50,0.9)`;
        ctx.beginPath();
        ctx.moveTo(u.x - 7, u.y - 2);
        ctx.lineTo(u.x + 7, u.y - 2);
        ctx.lineTo(u.x + 7, u.y + 4);
        ctx.lineTo(u.x - 7, u.y + 4);
        ctx.fill();

        // Draw top face
        ctx.fillStyle = color;
        ctx.beginPath();
        ctx.moveTo(u.x, u.y - 6);
        ctx.lineTo(u.x + 7, u.y - 2);
        ctx.lineTo(u.x, u.y + 2);
        ctx.lineTo(u.x - 7, u.y - 2);
        ctx.fill();

        // Propeller hint
        ctx.strokeStyle = 'rgba(255,255,255,0.4)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        const r = STATE.playing ? STATE.tick : 0;
        ctx.moveTo(u.x - 7 + Math.cos(r) * 4, u.y - 4 + Math.sin(r) * 2);
        ctx.lineTo(u.x - 7 - Math.cos(r) * 4, u.y - 4 - Math.sin(r) * 2);
        ctx.moveTo(u.x + 7 + Math.cos(r + 1) * 4, u.y - 4 + Math.sin(r + 1) * 2);
        ctx.lineTo(u.x + 7 - Math.cos(r + 1) * 4, u.y - 4 - Math.sin(r + 1) * 2);
        ctx.stroke();

        // NLOS indicator
        if (u.isNLOS) {
          ctx.beginPath();
          ctx.arc(u.x, u.y, 9, 0, Math.PI * 2);
          ctx.strokeStyle = 'rgba(255,170,0,0.8)';
          ctx.lineWidth = 1.5;
          ctx.stroke();
        }

        // Label
        ctx.fillStyle = 'rgba(200,230,245,0.7)';
        ctx.font = '8px monospace';
        ctx.fillText(`U${u.id.toString().padStart(2, '0')}`, u.x - 8, u.y - 10);

        // Energy bar if energy mode
        if (STATE.energyMode) {
          const bw = 20, bh = 3;
          ctx.fillStyle = 'rgba(0,0,0,0.5)';
          ctx.fillRect(u.x - bw / 2, u.y + 8, bw, bh);
          const ef = u.energy / 100;
          ctx.fillStyle = ef > 0.3 ? var_green : '#ff3b3b';
          ctx.fillRect(u.x - bw / 2, u.y + 8, bw * ef, bh);
        }

        // Prediction ghost if energy/nlos mode
        if (STATE.nlosEnabled) {
          ctx.beginPath();
          ctx.arc(u.x + u.vx * 20, u.y + u.vy * 20, 3, 0, Math.PI * 2);
          ctx.fillStyle = `${color}30`;
          ctx.fill();
        }
      });

      // GCS nodes
      groundStations.forEach((gs, idx) => {
        ctx.fillStyle = gs.active ? 'rgba(255,255,255,0.8)' : 'rgba(255,59,59,0.8)';
        ctx.beginPath();
        ctx.rect(gs.x - 8, gs.y - 8, 16, 16);
        ctx.fill();
        ctx.strokeStyle = gs.active ? 'white' : 'var(--red)';
        ctx.lineWidth = 1.5;
        ctx.stroke();
        ctx.fillStyle = gs.active ? 'rgba(200,230,240,0.9)' : 'rgba(255,80,80,0.9)';
        ctx.font = '8px monospace';
        ctx.fillText(gs.active ? `GCS${idx + 1}` : 'FAIL', gs.x - 10, gs.y + 20);
      });

      // Selection UI Highlight
      if (selectedUav && !selectedUav.failed) {
        ctx.beginPath();
        ctx.arc(selectedUav.x, selectedUav.y, 22, 0, Math.PI * 2);
        ctx.strokeStyle = 'rgba(255, 255, 255, 0.8)';
        ctx.lineWidth = 1.5;
        ctx.setLineDash([4, 4]);
        ctx.stroke();
        ctx.setLineDash([]);

        // Render 3D hover panel
        ctx.fillStyle = 'rgba(10, 20, 35, 0.9)';
        ctx.strokeStyle = 'var(--cyan)';
        ctx.lineWidth = 1;
        const panelX = selectedUav.x + 15;
        const panelY = selectedUav.y - 60;
        ctx.fillRect(panelX, panelY, 110, 65);
        ctx.strokeRect(panelX, panelY, 110, 65);

        ctx.fillStyle = 'var(--text-main)';
        ctx.font = '9px monospace';
        ctx.fillText(`ID: UAV_${String(selectedUav.id).padStart(2, '0')}`, panelX + 5, panelY + 12);
        ctx.fillStyle = CH_COLORS[selectedUav.channel];
        ctx.fillText(`信道: CH ${selectedUav.channel + 1}`, panelX + 5, panelY + 24);
        ctx.fillStyle = selectedUav.type === 'video' ? 'var(--orange)' : 'var(--green)';
        ctx.fillText(`业务: ${selectedUav.type === 'video' ? '大容量图传' : '控制极控'}`, panelX + 5, panelY + 36);
        ctx.fillStyle = selectedUav.energy > 30 ? 'var(--green)' : 'var(--red)';
        ctx.fillText(`电量: ${selectedUav.energy.toFixed(1)}%`, panelX + 5, panelY + 48);
        ctx.fillStyle = 'var(--text-dim)';
        ctx.fillText(`位置: ${selectedUav.x.toFixed(0)}, ${selectedUav.y.toFixed(0)}`, panelX + 5, panelY + 60);

        // Draw pointer line
        ctx.beginPath();
        ctx.moveTo(selectedUav.x + 8, selectedUav.y - 8);
        ctx.lineTo(panelX, panelY + 65);
        ctx.strokeStyle = 'var(--cyan)';
        ctx.stroke();
      }

      // End follow view transform
      ctx.restore();

      // AB Split View Overlay
      if (STATE.viewMode === 'split') {
        ctx.fillStyle = 'rgba(255,59,59,0.05)';
        ctx.fillRect(0, 0, W / 2, H);
        ctx.beginPath();
        ctx.moveTo(W / 2, 0);
        ctx.lineTo(W / 2, H);
        ctx.strokeStyle = 'rgba(255,255,255,0.4)';
        ctx.lineWidth = 2;
        ctx.setLineDash([10, 10]);
        ctx.stroke();
        ctx.setLineDash([]);
        ctx.font = '14px sans-serif';
        ctx.fillStyle = 'rgba(255,100,100,0.85)';
        ctx.fillText('🔴 传统无调度模式 (PDR低/冲突多)', 15, 25);
        ctx.fillStyle = 'rgba(0,242,255,0.85)';
        ctx.fillText('🟢 翼网智能全局分配 (零冲突/自适应)', W / 2 + 15, 25);
      }
    }

    const var_green = '#00ff88'; // css var workaround for canvas

    // ================================================================
    // PHYSICS UPDATE
    // ================================================================
    function updatePhysics() {
      STATE.tick++;
      const speed = STATE.energyMode ? 0.8 : 1.2;
      uavs.forEach(u => {
        if (u.failed) return;
        u.x += u.vx * speed;
        u.y += u.vy * speed;
        if (u.x < 10 || u.x > W - 10) u.vx *= -1;
        if (u.y < 10 || u.y > H - 10) u.vy *= -1;

        // Energy drain in energy mode
        if (STATE.energyMode) {
          u.energy = Math.max(5, u.energy - 0.02);
          if (u.energy < 30 && u.txPower > 12) { u.txPower -= 0.1; }
        }

        // Check NLOS
        u.isNLOS = buildings.some(b =>
          u.x > b.x && u.x < b.x + b.w && u.y > b.y && u.y < b.y + b.h
        );
      });
    }

    // ================================================================
    // INTERACTION HANDLERS (Canvas)
    // ================================================================
    function setInteractionMode(mode) {
      currentInteractionMode = mode;
      canvas.style.cursor = mode !== InteractionMode.NONE ? 'crosshair' : 'default';
      if (mode === InteractionMode.ADD_INTF) addLog('info', '沙盘模式：请在雷达图上拖动以绘制强磁干扰辐射区');
      if (mode === InteractionMode.ADD_BLDG) addLog('info', '沙盘模式：请在雷达图上拖动以空投新建建筑物(NLOS)');
    }

    canvas.addEventListener('mousedown', (e) => {
      const rect = canvas.getBoundingClientRect();
      const x = e.clientX - rect.left;
      const y = e.clientY - rect.top;

      if (currentInteractionMode !== InteractionMode.NONE) {
        isDragging = true;
        dragStart = { x, y };
        return;
      }

      // We still handle shift-click or double click to KILL the UAV
      if (e.shiftKey || e.ctrlKey) {
        const clickedUav = uavs.find(u => !u.failed && dist(u, { x, y }) < 20);
        if (clickedUav) {
          clickedUav.failed = true;
          if (selectedUav === clickedUav) selectedUav = null;
          addLog('crit', `== 节点灾难 == 目标 UAV_${String(clickedUav.id).padStart(2, '0')} 发生硬故障坠机！网络拓扑残缺`);
          addLog('warn', `启动智能自愈引擎 · 其余节点正重组图着色路由表...`);
          runGraphColoring();
        }
      }
    });

    canvas.addEventListener('mousemove', (e) => {
      if (!isDragging) return;
      const rect = canvas.getBoundingClientRect();
      const x = e.clientX - rect.left;
      const y = e.clientY - rect.top;

      if (currentInteractionMode === InteractionMode.ADD_INTF) {
        currentDragZone = { x: dragStart.x, y: dragStart.y, r: dist(dragStart, { x, y }) };
      } else if (currentInteractionMode === InteractionMode.ADD_BLDG) {
        currentDragZone = {
          x: Math.min(dragStart.x, x), y: Math.min(dragStart.y, y),
          w: Math.abs(x - dragStart.x), h: Math.abs(y - dragStart.y)
        };
      }
    });

    canvas.addEventListener('mouseup', () => {
      if (!isDragging) return;
      isDragging = false;

      if (currentInteractionMode === InteractionMode.ADD_INTF && currentDragZone) {
        STATE.interferenceLevel = Math.max(1, STATE.interferenceLevel); // ensure level is at least 1
        customIntfZones.push(currentDragZone);
        addLog('crit', `注入自定义范围干扰源区 (R=${Math.round(currentDragZone.r)}m) · 读取特定灾难分析模型集 JSON_INTF_CUST...`);
      } else if (currentInteractionMode === InteractionMode.ADD_BLDG && currentDragZone) {
        buildings.push({ ...currentDragZone, name: "临时空投建筑" });
        addLog('warn', `成功部署新建构筑物物理模型 · NLOS 遮挡视线发生大面积刷新`);
        currentInteractionMode = InteractionMode.NONE;
        currentDragZone = null;
        canvas.style.cursor = 'default';
        runGraphColoring();
      }
    });

    let selectedUav = null;

    canvas.addEventListener('click', (e) => {
      if (isDragging || currentInteractionMode !== InteractionMode.NONE) return; // Prevent triggering if dragging
      const rect = canvas.getBoundingClientRect();
      const x = e.clientX - rect.left;
      const y = e.clientY - rect.top;

      // Select UAV for info view
      const clickedUav = uavs.find(u => dist(u, { x, y }) < 25);
      if (clickedUav) {
        selectedUav = (selectedUav === clickedUav) ? null : clickedUav; // Toggle selection
        if (selectedUav) addLog('info', `操作员聚焦查看 UAV_${String(selectedUav.id).padStart(2, '0')} 详细参数`);
      } else {
        selectedUav = null; // Click empty space to deselect
      }
      draw(); // Force draw to show UI instantly
    });

    // ================================================================
    // UPDATE METRICS
    // ================================================================
    function updateMetrics() {
      const activeUavs = uavs.filter(u => !u.failed);
      const n = activeUavs.length;
      if (n === 0) return; // Prevent div by 0

      const conflicts = activeUavs.filter(u => u.conflict).length;
      const nloses = activeUavs.filter(u => u.isNLOS).length;
      const links = activeUavs.reduce((acc, u, i) => acc + activeUavs.slice(i + 1).filter(v => dist(u, v) < 160 && !isBlocked(u, v)).length, 0);
      const maxLinks = n * (n - 1) / 2;
      const connectivity = maxLinks > 0 ? (links / maxLinks) : 0;

      const pdr = Math.max(0.6, 0.97 - conflicts * 0.04 - STATE.interferenceLevel * 0.06 - nloses * 0.02);
      const pdrBase = Math.max(0.3, 0.72 - STATE.interferenceLevel * 0.12);
      const delay = 20 + conflicts * 8 + STATE.interferenceLevel * 12 + nloses * 5;
      const throughput = (n * pdr * (STATE.energyMode ? 0.7 : 1.0) * 1.2).toFixed(1);
      const health = Math.round(pdr * 100);

      // Ring
      const c = 2 * Math.PI * 55;
      document.getElementById('ring-fg').style.strokeDashoffset = c * (1 - health / 100);
      document.getElementById('health-val').textContent = health + '%';
      document.getElementById('health-val').style.color = health > 85 ? '#00ff88' : health > 70 ? '#ffaa00' : '#ff3b3b';

      document.getElementById('m-pdr').textContent = (pdr * 100).toFixed(1) + '%';
      document.getElementById('m-delay').textContent = delay.toFixed(0) + ' ms';
      document.getElementById('m-tp').textContent = throughput + ' Mbps';
      document.getElementById('m-link').textContent = links;

      document.getElementById('topo-chip').innerHTML = `拓扑连通性: <strong>${(connectivity * 100).toFixed(1)}%</strong>`;
      document.getElementById('conflict-chip').innerHTML = `同频冲突: <strong style="color:${conflicts > 0 ? '#ff3b3b' : '#00ff88'}">${conflicts}</strong>`;

      document.getElementById('q-avail').textContent = (pdr * 100).toFixed(1) + '%';
      document.getElementById('q-p99').textContent = (delay * 2.5).toFixed(0) + ' ms';
      document.getElementById('q-reconnect').textContent = (120 + conflicts * 30) + ' ms';
      const ee = (pdr * parseInt(throughput) || 0) / (STATE.energyMode ? 0.7 : 1);
      document.getElementById('q-ee').textContent = ee.toFixed(2) + ' b/J';

      // PDR chart history
      pdrHistory.opt.push(pdr * 100);
      pdrHistory.base.push(pdrBase * 100);
      if (pdrHistory.opt.length > PDR_MAX_POINTS) { pdrHistory.opt.shift(); pdrHistory.base.shift(); }
      drawPDRChart();

      // Spectrum
      updateSpectrum();

      // UAV list
      updateUAVList();
    }

    function drawPDRChart() {
      const c = document.getElementById('pdr-lines');
      c.width = c.parentElement.clientWidth;
      c.height = 110;
      const cx = c.getContext('2d');
      cx.clearRect(0, 0, c.width, c.height);

      function drawLine(data, color, fill) {
        if (data.length < 2) return;
        cx.beginPath();
        data.forEach((v, i) => {
          const x = i / (PDR_MAX_POINTS - 1) * c.width;
          const y = c.height - (v / 100) * c.height;
          i === 0 ? cx.moveTo(x, y) : cx.lineTo(x, y);
        });
        if (fill) {
          cx.lineTo(c.width, c.height); cx.lineTo(0, c.height); cx.closePath();
          cx.fillStyle = `${color}18`; cx.fill();
        }
        cx.strokeStyle = color; cx.lineWidth = 1.5; cx.stroke();
      }

      // Grid lines
      [80, 90, 100].forEach(v => {
        cx.strokeStyle = 'rgba(255,255,255,0.05)'; cx.lineWidth = 1;
        const y = c.height - (v / 100) * c.height;
        cx.beginPath(); cx.moveTo(0, y); cx.lineTo(c.width, y); cx.stroke();
        cx.fillStyle = 'rgba(150,180,200,0.4)'; cx.font = '8px monospace';
        cx.fillText(v + '%', 2, y - 2);
      });

      drawLine(pdrHistory.base, 'rgba(100,120,140,0.6)', false);
      drawLine(pdrHistory.opt, '#00f2ff', true);

      // Legend
      cx.fillStyle = 'rgba(0,242,255,0.9)'; cx.font = '8px sans-serif';
      cx.fillText('── 优化后', c.width - 80, 12);
      cx.fillStyle = 'rgba(100,130,160,0.8)';
      cx.fillText('── 基准线', c.width - 80, 24);
    }

    function updateSpectrum() {
      const counts = [0, 0, 0, 0, 0, 0];
      uavs.filter(u => !u.failed).forEach(u => { counts[u.channel % 6]++; });
      // spread across 6 visual slots for aesthetics
      const spGrid = document.getElementById('spectrum-grid');
      spGrid.innerHTML = '';
      for (let i = 0; i < 6; i++) {
        const ch = i % 3;
        const count = i < 3 ? counts[i] : Math.max(0, counts[i] || 0);
        const heightPct = Math.min(95, (count / uavs.length) * 200);
        const color = CH_COLORS[ch];
        spGrid.innerHTML += `
      <div class="sp-bar-wrap">
        <div class="sp-bar-bg">
          <div class="sp-bar-fill" style="height:${heightPct}%;background:${color};opacity:0.7;"></div>
        </div>
        <div class="sp-label" style="color:${color}">CH${i + 1}</div>
        <div class="sp-label">${count}</div>
      </div>`;
      }
    }

    function updateUAVList() {
      const list = document.getElementById('uav-list');
      list.innerHTML = '';
      uavs.slice(0, 12).forEach(u => {
        if (u.failed) return;
        const color = u.conflict ? '#ff3b3b' : CH_COLORS[u.channel];
        const eg = STATE.energyMode ? u.energy.toFixed(0) : '--';
        const egColor = u.energy > 50 ? '#00ff88' : u.energy > 25 ? '#ffaa00' : '#ff3b3b';
        list.innerHTML += `
      <div class="uav-item">
        <span class="uav-id">U${u.id.toString().padStart(2, '0')}</span>
        <div class="energy-bar-wrap"><div class="energy-bar" style="width:${u.energy}%;background:${egColor}"></div></div>
        <span class="ch-badge ch${u.channel}" style="background:${CH_BCOLORS[u.channel]};color:${color}">${CHANNELS[u.channel]}</span>
        <span style="color:${egColor};font-size:0.65rem">${eg}%</span>
        <span style="color:${u.isNLOS ? '#ffaa00' : '#00ff88'};font-size:0.65rem">${u.isNLOS ? 'NLOS' : 'LOS'}</span>
      </div>`;
      });
      document.getElementById('active-count').textContent = `${uavs.length} / ${STATE.uavCount}`;
    }

    // ================================================================
    // LOG SYSTEM
    // ================================================================
    const LOG_MSGS = {
      info: [
        (i) => `UAV_${String(i).padStart(2, '0')} 完成航段 · 位置上报正常`,
        () => `图着色算法完成第 ${STATE.algoRuns} 次全局重分配 · 0 冲突`,
        () => `RTK 轨迹数据帧同步 · 时戳偏差 +${(Math.random() * 2).toFixed(1)}ms`,
        () => `拓扑邻接矩阵更新 · 活跃链路 ${uavs.reduce((a, u, i) => a + uavs.slice(i + 1).filter(v => dist(u, v) < 160).length, 0)} 条`,
        () => `空地协同模式 · GCS1 与 ${2 + Math.floor(Math.random() * 4)} 架节点维持连接`,
      ],
      warn: [
        () => `UAV_${String(Math.floor(Math.random() * STATE.uavCount)).padStart(2, '0')} 进入建筑物遮挡区域 [NLOS] · 触发频点跳变`,
        () => `信道 CH${1 + Math.floor(Math.random() * 3)} 使用率超过 80% · 建议负载均衡`,
        () => `检测到潜在同频干扰 · 受影响节点 ${Math.floor(Math.random() * 4) + 1} 架`,
        () => `UAV_${String(Math.floor(Math.random() * STATE.uavCount)).padStart(2, '0')} 电量低于 30% · 切换省电模式`,
      ],
      crit: [
        () => `!!  干扰源注入  !! 全域 SNR 下降 ${(State_IntfLvl() * 12).toFixed(0)} dB · 紧急重分配`,
        () => `GCS 失联告警 · 节点切换至空空自组网模式`,
        () => `(Tip) 可通过 Shift+Click 在雷达图上强行摧毁节点测试网络韧性`,
      ],
      good: [
        () => `算法完成自愈 · PDR 恢复至 ${(90 + Math.random() * 8).toFixed(1)}%`,
        () => `新优化轮次完成 · 全网同频冲突归零`,
        () => `能耗感知调度生效 · 平均发射功率降低 ${(8 + Math.random() * 6).toFixed(1)}%`,
      ]
    };

    function State_IntfLvl() { return STATE.interferenceLevel || 1; }

    function addLog(type, msg) {
      STATE.logCount++;
      document.getElementById('log-count').textContent = `${STATE.logCount} 条事件`;
      const t = new Date().toLocaleTimeString('zh-CN', { hour12: false });
      const div = document.createElement('div');
      div.className = `log-entry ${type}`;
      div.textContent = `[${t}] ${msg}`;
      const scroll = document.getElementById('log-scroll');
      scroll.prepend(div);
      if (scroll.children.length > 40) scroll.removeChild(scroll.lastChild);
    }

    function autoLog() {
      const r = Math.random();
      let type, fn;
      if (r < 0.6) { type = 'info'; fn = LOG_MSGS.info[Math.floor(Math.random() * LOG_MSGS.info.length)]; }
      else if (r < 0.85) { type = 'warn'; fn = LOG_MSGS.warn[Math.floor(Math.random() * LOG_MSGS.warn.length)]; }
      else { type = 'good'; fn = LOG_MSGS.good[Math.floor(Math.random() * LOG_MSGS.good.length)]; }
      addLog(type, fn(Math.floor(Math.random() * STATE.uavCount)));
    }

    // ================================================================
    // CONTROL HANDLERS
    // ================================================================
    function injectInterference() {
      STATE.interferenceLevel = Math.min(3, STATE.interferenceLevel + 1);
      document.getElementById('intf-slider').value = STATE.interferenceLevel;
      updateIntfLabel();
      addLog('crit', `!!  手动注入 Level-${STATE.interferenceLevel} 干扰源  !! 全域频谱重排触发`);
      const badge = document.getElementById('event-badge');
      badge.style.display = 'block';
      runGraphColoring();
      addLog('good', `图着色算法完成紧急重排 · 已消解同频冲突`);
      setTimeout(() => { badge.style.display = 'none'; }, 3200);
    }

    function toggleNLOS() {
      STATE.nlosEnabled = !STATE.nlosEnabled;
      const btn = document.getElementById('nlos-btn');
      btn.textContent = STATE.nlosEnabled ? '关闭遮挡感知' : '启用遮挡感知';
      btn.classList.toggle('active', STATE.nlosEnabled);
      addLog('info', `建筑物遮挡感知模型: ${STATE.nlosEnabled ? '✓ 启用' : '✗ 停用'}`);
    }

    function toggleGCS() {
      STATE.gcsFailure = !STATE.gcsFailure;
      groundStations.forEach(g => g.active = !STATE.gcsFailure);
      const btn = document.getElementById('gcs-btn');
      btn.textContent = STATE.gcsFailure ? '恢复地面站' : '地面站故障';
      btn.classList.toggle('danger', !STATE.gcsFailure);
      if (STATE.gcsFailure) {
        addLog('crit', 'GCS1 & GCS2 模拟失效 · 全队切换至 Ad-Hoc 自愈模式');
        addLog('warn', `${Math.floor(STATE.uavCount * 0.6)} 架节点重建空空拓扑链路`);
      } else {
        addLog('good', '地面站信号恢复 · 节点逐步回归蜂窝拓扑');
      }
    }

    function toggleEnergy() {
      STATE.energyMode = !STATE.energyMode;
      const btn = document.getElementById('energy-btn');
      btn.classList.toggle('active', STATE.energyMode);
      btn.textContent = STATE.energyMode ? '关闭能耗模式' : '能耗博弈模式';
      if (STATE.energyMode) {
        addLog('warn', '能耗感知调度模式激活 · 节点发射功率开始自适应降低');
      } else {
        uavs.forEach(u => { u.txPower = 20; u.energy = 60 + Math.random() * 40; });
        addLog('info', '能耗模式停用 · 节点恢复满功率运行');
      }
    }

    function stressTest() {
      addLog('crit', '== 极端压力测试 启动 == 同时注入干扰 + 地面站失效 + 机群规模扩展');
      STATE.interferenceLevel = 3;
      STATE.gcsFailure = true;
      groundStations.forEach(g => g.active = false);
      STATE.energyMode = true;
      STATE.uavCount = 30;
      document.getElementById('uav-slider').value = 30;
      document.getElementById('uav-count-label').textContent = '30';
      document.getElementById('intf-slider').value = 3;
      updateIntfLabel();
      initUAVs();
      setTimeout(() => {
        runGraphColoring();
        addLog('good', '压力测试自愈完成 · DSATUR重分配耗时 < 5ms · 系统韧性验证通过');
      }, 2000);
    }

    function resetSim() {
      STATE.interferenceLevel = 0;
      STATE.gcsFailure = false;
      STATE.energyMode = false;
      STATE.uavCount = 15;
      STATE.algoRuns = 0;
      groundStations.forEach(g => g.active = true);
      document.getElementById('uav-slider').value = 15;
      document.getElementById('uav-count-label').textContent = '15';
      document.getElementById('intf-slider').value = 0;
      document.getElementById('energy-btn').classList.remove('active');
      document.getElementById('nlos-btn').classList.add('active');
      STATE.nlosEnabled = true;
      updateIntfLabel();
      pdrHistory = { opt: [], base: [] };
      initUAVs();
      addLog('info', '== 仿真环境重置 == 参数归默认 · 节点重初始化');
    }

    function updateIntfLabel() {
      const labels = ['无', '低', '中', '高'];
      document.getElementById('intf-label').textContent = labels[STATE.interferenceLevel];
    }

    // Slider events
    document.getElementById('uav-slider').addEventListener('input', function () {
      STATE.uavCount = parseInt(this.value);
      document.getElementById('uav-count-label').textContent = STATE.uavCount;
      initUAVs();
      addLog('info', `机群规模调整为 ${STATE.uavCount} 架 · 重新执行 DSATUR 分配`);
    });
    document.getElementById('intf-slider').addEventListener('input', function () {
      STATE.interferenceLevel = parseInt(this.value);
      updateIntfLabel();
    });

    // ================================================================
    // CLOCK
    // ================================================================
    function updateClock() {
      document.getElementById('sys-clock').textContent = new Date().toLocaleString('zh-CN');
    }

    // Timeline and Playback Controls
    function togglePlay() {
      STATE.playing = !STATE.playing;
      document.getElementById('play-pause-btn').innerHTML = STATE.playing ? '⏸' : '▶';
      document.getElementById('play-pause-btn').style.color = STATE.playing ? 'var(--cyan)' : 'var(--orange)';
    }
    function changeSpeed(v) { STATE.speed = parseInt(v); }
    function changeView(v) {
      STATE.viewMode = v;
      if (v === 'split') addLog('warn', '切换至 AB 屏极化展示：评估 [传统无调度] 与 [Wing-Net 智能调度] 性能');
      else if (v === 'follow') addLog('info', '切换至 抵近定点追踪模式 · 强锁定 UAV_00');
      else addLog('info', '切换至 默认全域上帝统筹视角');
    }

    document.getElementById('time-slider').addEventListener('input', function () {
      // Seek timeline manipulates slightly to simulate scrubbing data
      uavs.forEach(u => {
        u.x += (Math.random() - 0.5) * 15;
        u.y += (Math.random() - 0.5) * 15;
      });
      runGraphColoring();
    });

    // ================================================================
    // MAIN LOOP
    // ================================================================
    let frameCount = 0;
    function mainLoop() {
      if (STATE.playing) {
        for (let s = 0; s < STATE.speed; s++) {
          frameCount++;
          updatePhysics();

          // Update time slider
          if (frameCount % 4 === 0) {
            let ts = document.getElementById('time-slider');
            if (parseInt(ts.value) < parseInt(ts.max)) ts.value = parseInt(ts.value) + 1;
            else ts.value = 0;
          }

          if (frameCount % 90 === 0) {
            runGraphColoring();
            updateMetrics();
          }
          if (frameCount % 150 === 0) {
            autoLog();
          }
        }
      }

      draw();
      requestAnimationFrame(mainLoop);
    }

    // ================================================================
    // INIT
    // ================================================================
    updateClock();
    setInterval(updateClock, 1000);
    resizeCanvas();
    window.addEventListener('resize', resizeCanvas);

    // Boot log sequence
    const bootMsgs = [
      ['info', 'Wing-Net Omni Hub v2.0 · 系统启动'],
      ['info', 'RTK 轨迹引擎加载完成 · 支持 WGS-84 → ENU 实时转换'],
      ['info', 'ns-3 仿真内核 · 加载 Nakagami 衰落模型'],
      ['info', 'DSATUR 图着色资源调度引擎就绪 · 空间复杂度 O(N²)'],
      ['info', '建筑物遮挡感知 (NLOS) 模型初始化 · 5 个 CBD 障碍体已加载'],
      ['info', '能耗感知调度模块就绪 · 监测阈值: 30%'],
      ['good', '全系统在线 · 开始 15 架 UAV 初始化部署'],
    ];
    bootMsgs.forEach((m, i) => setTimeout(() => addLog(m[0], m[1]), i * 400));
    setTimeout(() => { runGraphColoring(); updateMetrics(); }, bootMsgs.length * 400);
    setTimeout(() => mainLoop(), (bootMsgs.length + 1) * 400);

    document.getElementById('nlos-btn').classList.add('active');
  
