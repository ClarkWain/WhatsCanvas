"use strict";

const $ = (id) => document.getElementById(id);
const clamp = (value, min, max) => Math.max(min, Math.min(max, value));
const crisp = (value, digits = 2) => Number(value).toFixed(digits).replace(/\.00$/, "").replace(/(\.\d)0$/, "$1");

function setSegmented(id, onChange) {
  const root = $(id);
  let value = root.querySelector(".active")?.dataset.value;
  root.addEventListener("click", (event) => {
    const button = event.target.closest("button[data-value]");
    if (!button) return;
    root.querySelectorAll("button").forEach((item) => item.classList.toggle("active", item === button));
    value = button.dataset.value;
    onChange(value);
  });
  return () => value;
}

function metric(label, value, note = "") {
  return `<div class="metric"><span>${label}</span><strong>${value}</strong>${note ? `<small>${note}</small>` : ""}</div>`;
}

function grid(ctx, width, height, step = 40) {
  ctx.save();
  ctx.fillStyle = "#0b1118";
  ctx.fillRect(0, 0, width, height);
  ctx.strokeStyle = "rgba(127, 231, 255, .08)";
  ctx.lineWidth = 1;
  for (let x = 0; x <= width; x += step) {
    ctx.beginPath(); ctx.moveTo(x + .5, 0); ctx.lineTo(x + .5, height); ctx.stroke();
  }
  for (let y = 0; y <= height; y += step) {
    ctx.beginPath(); ctx.moveTo(0, y + .5); ctx.lineTo(width, y + .5); ctx.stroke();
  }
  ctx.restore();
}

function label(ctx, text, x, y, color = "#8ea2b6") {
  ctx.save();
  ctx.font = "13px ui-monospace, SFMono-Regular, Consolas, monospace";
  ctx.fillStyle = color;
  ctx.fillText(text, x, y);
  ctx.restore();
}

function roundedPath(ctx, x, y, width, height, radius) {
  const r = clamp(radius, 0, Math.min(width, height) / 2);
  ctx.beginPath();
  ctx.moveTo(x + r, y);
  ctx.lineTo(x + width - r, y);
  ctx.quadraticCurveTo(x + width, y, x + width, y + r);
  ctx.lineTo(x + width, y + height - r);
  ctx.quadraticCurveTo(x + width, y + height, x + width - r, y + height);
  ctx.lineTo(x + r, y + height);
  ctx.quadraticCurveTo(x, y + height, x, y + height - r);
  ctx.lineTo(x, y + r);
  ctx.quadraticCurveTo(x, y, x + r, y);
  ctx.closePath();
}

// 01 — command lifecycle ----------------------------------------------------
function initFrameLab() {
  const canvas = $("frameCanvas");
  const ctx = canvas.getContext("2d");
  let recording = false;
  let queue = [];
  let scene = [];
  let presented = 0;
  let sequence = 0;

  function addLog(text, kind = "") {
    const item = document.createElement("li");
    item.textContent = text;
    if (kind) item.className = kind;
    $("frameLog").prepend(item);
    while ($("frameLog").children.length > 6) $("frameLog").lastElementChild.remove();
  }

  function drawScene() {
    grid(ctx, canvas.width, canvas.height, 38);
    if (!scene.length) {
      ctx.fillStyle = "#60758a";
      ctx.font = "16px ui-monospace, Consolas, monospace";
      ctx.fillText("framebuffer has not been written", 218, 214);
      return;
    }
    for (const command of scene) {
      if (command.type === "round") {
        roundedPath(ctx, 82, 88, 230, 150, 38);
        ctx.fillStyle = "#ff8a48"; ctx.fill();
      } else if (command.type === "image") {
        const gradient = ctx.createLinearGradient(350, 90, 585, 245);
        gradient.addColorStop(0, "#5fe0ff"); gradient.addColorStop(1, "#7357ff");
        ctx.fillStyle = gradient; ctx.fillRect(350, 90, 235, 155);
        ctx.strokeStyle = "rgba(255,255,255,.35)";
        for (let i = 0; i < 6; i++) { ctx.beginPath(); ctx.moveTo(350 + i * 47, 90); ctx.lineTo(350 + i * 47, 245); ctx.stroke(); }
      } else {
        ctx.fillStyle = "#f3f7fb";
        ctx.font = "700 35px system-ui, sans-serif";
        ctx.fillText("WhatsCanvas", 220, 335);
      }
    }
    label(ctx, `executed ${scene.length} command${scene.length === 1 ? "" : "s"}`, 20, 400, "#67e8f9");
  }

  function renderUI() {
    $("frameState").textContent = recording ? "RECORDING" : "IDLE";
    $("frameState").className = `status ${recording ? "recording" : "idle"}`;
    $("queueCount").textContent = queue.length;
    $("commandQueue").innerHTML = queue.length
      ? queue.map((item, i) => `<span><i>${String(i + 1).padStart(2, "0")}</i>${item.api}</span>`).join("")
      : '<span class="empty">队列为空</span>';
    $("presentCount").textContent = `presented: ${presented}`;
  }

  document.querySelectorAll("[data-frame-action]").forEach((button) => button.addEventListener("click", () => {
    const action = button.dataset.frameAction;
    if (action === "reset") {
      recording = false; queue = []; scene = []; presented = 0; sequence = 0;
      $("frameLog").replaceChildren(); addLog("实验已重置"); drawScene(); renderUI(); return;
    }
    if (action === "begin") {
      if (queue.length) addLog(`beginFrame 丢弃了 ${queue.length} 条未提交命令`, "warn");
      queue = []; recording = true; addLog("开始录制；framebuffer 没有变化"); renderUI(); return;
    }
    if (["round", "image", "text"].includes(action)) {
      if (!recording) { addLog("先调用 beginFrame()", "warn"); return; }
      const api = action === "round" ? "drawRoundRect" : action === "image" ? "drawImage" : "drawText";
      queue.push({ type: action, api: `${api}()`, id: ++sequence });
      addLog(`${api} 已进入 queue，尚未绘制`); renderUI(); return;
    }
    if (action === "end") {
      if (!recording) { addLog("当前没有正在录制的 frame", "warn"); return; }
      scene = queue.slice(); queue = []; recording = false; drawScene();
      addLog(`endFrame 执行并消费了 ${scene.length} 条命令`, "ok"); renderUI(); return;
    }
    if (action === "present") {
      presented += 1; addLog("present/read：读取当前 framebuffer"); renderUI();
    }
  }));
  drawScene(); renderUI();
}

// 02 — round rect -----------------------------------------------------------
function initRoundRectLab() {
  const canvas = $("roundCanvas");
  const ctx = canvas.getContext("2d");
  let style = "fill";
  const getStyle = setSegmented("rrStyle", (value) => { style = value; render(); });
  const controls = ["rrWidth", "rrHeight", "rrRadius", "rrZoom", "rrMesh"];
  controls.forEach((id) => $(id).addEventListener("input", render));

  function render() {
    style = getStyle() || style;
    const w = +$("rrWidth").value;
    const h = +$("rrHeight").value;
    const requested = +$("rrRadius").value;
    const zoom = +$("rrZoom").value;
    const radius = requested * Math.min(1, w / Math.max(2 * requested, 1), h / Math.max(2 * requested, 1));
    const physicalRadius = radius * Math.max(zoom, .001);
    const estimated = Math.ceil(radius * .35);
    const cosine = clamp(1 - .1 / Math.max(physicalRadius, .1), -1, 1);
    const maximumStep = 2 * Math.acos(cosine);
    const tolerance = Math.ceil((Math.PI / 2) / Math.max(maximumStep, .0001));
    const segments = clamp(Math.max(estimated, tolerance), 4, 24);
    const fast = style === "fill";
    $("rrWidthOut").textContent = `${w}px`; $("rrHeightOut").textContent = `${h}px`;
    $("rrRadiusOut").textContent = `${requested}px`; $("rrZoomOut").textContent = `${zoom}×`;

    grid(ctx, canvas.width, canvas.height, 40);
    const scale = Math.min(1, 600 / w, 330 / h);
    const dw = w * scale, dh = h * scale, dr = radius * scale;
    const x = (canvas.width - dw) / 2, y = (canvas.height - dh) / 2;
    const points = [];
    const corners = [
      [x + dw - dr, y + dr, -Math.PI / 2, 0],
      [x + dw - dr, y + dh - dr, 0, Math.PI / 2],
      [x + dr, y + dh - dr, Math.PI / 2, Math.PI],
      [x + dr, y + dr, Math.PI, Math.PI * 1.5]
    ];
    for (const [cx, cy, a0, a1] of corners) {
      for (let i = 0; i <= segments; i++) {
        const a = a0 + (a1 - a0) * (i / segments);
        points.push({ x: cx + Math.cos(a) * dr, y: cy + Math.sin(a) * dr });
      }
    }
    const center = { x: x + dw / 2, y: y + dh / 2 };
    ctx.beginPath(); points.forEach((p, i) => i ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y)); ctx.closePath();
    if (style === "gradient") {
      const gradient = ctx.createLinearGradient(x, y, x + dw, y + dh);
      gradient.addColorStop(0, "#5fe0ff"); gradient.addColorStop(1, "#7b61ff");
      ctx.fillStyle = gradient; ctx.fill();
    } else if (style === "stroke") {
      ctx.strokeStyle = "#5fe0ff"; ctx.lineWidth = 14; ctx.stroke();
    } else { ctx.fillStyle = "rgba(95,224,255,.3)"; ctx.fill(); }
    if ($("rrMesh").checked && style !== "stroke") {
      ctx.strokeStyle = "rgba(95,224,255,.22)"; ctx.lineWidth = 1;
      points.forEach((p) => { ctx.beginPath(); ctx.moveTo(center.x, center.y); ctx.lineTo(p.x, p.y); ctx.stroke(); });
    }
    ctx.strokeStyle = fast ? "#5fe0ff" : "#ff9a5c"; ctx.lineWidth = 2;
    ctx.beginPath(); points.forEach((p, i) => i ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y)); ctx.closePath(); ctx.stroke();
    ctx.fillStyle = "#ff9a5c";
    points.forEach((p) => { ctx.beginPath(); ctx.arc(p.x, p.y, 2.2, 0, Math.PI * 2); ctx.fill(); });
    if (requested !== radius) label(ctx, `radius constrained: ${requested} → ${crisp(radius)} px`, 20, 28, "#ff9a5c");
    else label(ctx, `radius accepted: ${requested}px`, 20, 28, "#67e8f9");

    $("roundMetrics").innerHTML = metric("effective radius", `${crisp(radius)} px`, requested !== radius ? "宽高不够，四角等比收缩" : "无需约束")
      + metric("segments / corner", segments, `physical radius ${crisp(physicalRadius)} px`)
      + metric("outline vertices", points.length, "教学轮廓含角端点")
      + metric("geometry reuse", fast ? "1 mesh / N instances" : "none", fast ? "尺寸相同即可复用" : "进入通用 Path");
    $("roundTrace").innerHTML = fast
      ? '<b class="pass">FAST PATH</b><code>fill + uniform radius → cached unit mesh → instance transform</code>'
      : `<b class="fail">GENERAL PATH</b><code>${style} 改变了绘制语义 → 构造轮廓 → tessellate</code>`;
  }
  render();
}

// 03 — image ---------------------------------------------------------------
function initImageLab() {
  const source = document.createElement("canvas"); source.width = source.height = 256;
  const sctx = source.getContext("2d");
  const colors = ["#23c6de", "#ff8957", "#745cff", "#efcf4a", "#27d49b", "#db4c79"];
  sctx.fillStyle = "#111923"; sctx.fillRect(0, 0, 256, 256);
  for (let y = 0; y < 4; y++) for (let x = 0; x < 4; x++) {
    sctx.fillStyle = colors[(x + y * 3) % colors.length];
    sctx.fillRect(x * 64 + 2, y * 64 + 2, 60, 60);
    sctx.fillStyle = "rgba(8,12,18,.72)"; sctx.font = "700 18px ui-monospace, monospace";
    sctx.fillText(`${x},${y}`, x * 64 + 13, y * 64 + 36);
    sctx.strokeStyle = "rgba(255,255,255,.25)";
    for (let n = 8; n < 60; n += 8) { sctx.beginPath(); sctx.moveTo(x * 64 + n, y * 64 + 2); sctx.lineTo(x * 64 + n, y * 64 + 62); sctx.stroke(); }
  }
  const srcCanvas = $("sourceCanvas"), srcCtx = srcCanvas.getContext("2d");
  const dstCanvas = $("destCanvas"), dstCtx = dstCanvas.getContext("2d");
  ["srcX", "srcY", "srcSize", "dstScale", "imageSampling", "flipV"].forEach((id) => $(id).addEventListener("input", render));

  function render() {
    const x = +$("srcX").value, y = +$("srcY").value, requested = +$("srcSize").value;
    const sw = Math.min(requested, 256 - x), sh = Math.min(requested, 256 - y);
    const scale = +$("dstScale").value, sampling = $("imageSampling").value, flip = $("flipV").checked;
    $("srcXOut").textContent = x; $("srcYOut").textContent = y; $("srcSizeOut").textContent = requested;
    $("dstScaleOut").textContent = `${scale}×`;
    srcCtx.imageSmoothingEnabled = false; srcCtx.clearRect(0, 0, 512, 512); srcCtx.drawImage(source, 0, 0, 512, 512);
    srcCtx.fillStyle = "rgba(255,138,72,.12)"; srcCtx.fillRect(x * 2, y * 2, sw * 2, sh * 2);
    srcCtx.strokeStyle = "#ff9a5c"; srcCtx.lineWidth = 4; srcCtx.strokeRect(x * 2 + 2, y * 2 + 2, sw * 2 - 4, sh * 2 - 4);
    label(srcCtx, `${x},${y} → ${x + sw},${y + sh}`, 12, 500, "#fff");

    grid(dstCtx, 512, 512, 32);
    const targetW = Math.min(sw * scale, 420), targetH = Math.min(sh * scale, 420);
    const dx = (512 - targetW) / 2, dy = (512 - targetH) / 2;
    dstCtx.save();
    dstCtx.imageSmoothingEnabled = sampling !== "nearest";
    dstCtx.imageSmoothingQuality = sampling === "mipmap" ? "high" : "medium";
    if (flip) { dstCtx.translate(0, 512); dstCtx.scale(1, -1); }
    const renderY = flip ? 512 - dy - targetH : dy;
    dstCtx.drawImage(source, x, y, sw, sh, dx, renderY, targetW, targetH);
    dstCtx.restore();
    dstCtx.strokeStyle = sampling === "nearest" ? "#ff9a5c" : "#5fe0ff"; dstCtx.lineWidth = 2; dstCtx.strokeRect(dx, dy, targetW, targetH);
    const u0 = x / 256, v0 = y / 256, u1 = (x + sw) / 256, v1 = (y + sh) / 256;
    $("uvReadout").innerHTML = `<span>UV0 <b>${crisp(u0, 3)}, ${crisp(v0, 3)}</b></span><span>UV1 <b>${crisp(u1, 3)}, ${crisp(v1, 3)}</b></span><small>1 source pixel = ${(1 / 256).toFixed(6)} UV</small>`;
    const batch = sampling === "linear";
    $("batchBadge").textContent = batch ? "BATCH: ELIGIBLE" : "BATCH: GENERAL";
    $("batchBadge").className = batch ? "pass-text" : "warn-text";
    $("samplingNote").textContent = sampling === "nearest"
      ? "NEAREST 保留硬边；WhatsCanvas 的 SpriteBatch 快路只接受 Linear + Clamp。"
      : sampling === "mipmap"
        ? "浏览器用高质量缩放近似预览；WhatsCanvas 的 Mipmap 需要纹理资源实际带有 mip levels。"
        : `${flip ? "V 方向已翻转；" : ""}Linear + Clamp 满足当前 SpriteBatch 采样条件。`;
  }
  render();
}

// 04 — path ----------------------------------------------------------------
function initPathLab() {
  const canvas = $("pathCanvas"), ctx = canvas.getContext("2d");
  const points = [{ x: 100, y: 390 }, { x: 245, y: 70 }, { x: 610, y: 80 }, { x: 745, y: 385 }];
  let stage = "curve", dragging = -1;
  const getStage = setSegmented("pathStage", (value) => { stage = value; render(); });
  ["pathSegments", "pathStroke"].forEach((id) => $(id).addEventListener("input", render));
  function cubic(t) {
    const u = 1 - t;
    return {
      x: u ** 3 * points[0].x + 3 * u * u * t * points[1].x + 3 * u * t * t * points[2].x + t ** 3 * points[3].x,
      y: u ** 3 * points[0].y + 3 * u * u * t * points[1].y + 3 * u * t * t * points[2].y + t ** 3 * points[3].y
    };
  }
  function polygon(poly, close = false) {
    ctx.beginPath(); poly.forEach((p, i) => i ? ctx.lineTo(p.x, p.y) : ctx.moveTo(p.x, p.y)); if (close) ctx.closePath();
  }
  function render() {
    stage = getStage() || stage;
    const n = +$("pathSegments").value, strokeWidth = +$("pathStroke").value;
    $("pathSegmentsOut").textContent = n; $("pathStrokeOut").textContent = `${strokeWidth}px`;
    const contour = Array.from({ length: n + 1 }, (_, i) => cubic(i / n));
    const fillPoly = [...contour, { x: points[3].x, y: 445 }, { x: points[0].x, y: 445 }];
    grid(ctx, canvas.width, canvas.height, 40);
    if (stage === "curve") {
      ctx.strokeStyle = "rgba(255,255,255,.2)"; ctx.lineWidth = 1.5; polygon(points); ctx.stroke();
      ctx.strokeStyle = "#5fe0ff"; ctx.lineWidth = 4; ctx.beginPath(); ctx.moveTo(points[0].x, points[0].y); ctx.bezierCurveTo(points[1].x, points[1].y, points[2].x, points[2].y, points[3].x, points[3].y); ctx.stroke();
      label(ctx, "M  cubicTo(P1, P2, P3)", 22, 30, "#67e8f9");
    } else if (stage === "contour") {
      ctx.strokeStyle = "#5fe0ff"; ctx.lineWidth = 2; polygon(contour); ctx.stroke();
      contour.forEach((p, i) => { ctx.fillStyle = i % 2 ? "#ff9a5c" : "#fff"; ctx.beginPath(); ctx.arc(p.x, p.y, 3.2, 0, Math.PI * 2); ctx.fill(); });
      label(ctx, "cubic verb no longer exists — only points remain", 22, 30, "#ff9a5c");
    } else if (stage === "fill") {
      const anchor = fillPoly[0];
      ctx.fillStyle = "rgba(95,224,255,.16)"; polygon(fillPoly, true); ctx.fill();
      ctx.strokeStyle = "rgba(95,224,255,.28)";
      for (let i = 1; i < fillPoly.length - 1; i++) { ctx.beginPath(); ctx.moveTo(anchor.x, anchor.y); ctx.lineTo(fillPoly[i].x, fillPoly[i].y); ctx.lineTo(fillPoly[i + 1].x, fillPoly[i + 1].y); ctx.closePath(); ctx.stroke(); }
      ctx.strokeStyle = "#5fe0ff"; ctx.lineWidth = 2; polygon(fillPoly, true); ctx.stroke();
      label(ctx, "teaching fan: contour → triangles", 22, 30, "#67e8f9");
    } else {
      if (stage === "aa") {
        ctx.strokeStyle = "rgba(95,224,255,.12)"; ctx.lineWidth = strokeWidth + 8; ctx.lineJoin = "round"; polygon(contour); ctx.stroke();
        ctx.strokeStyle = "rgba(255,154,92,.32)"; ctx.lineWidth = strokeWidth + 3; polygon(contour); ctx.stroke();
      }
      ctx.strokeStyle = "#5fe0ff"; ctx.lineWidth = strokeWidth; ctx.lineCap = "round"; ctx.lineJoin = "round"; polygon(contour); ctx.stroke();
      ctx.strokeStyle = "rgba(8,17,24,.65)"; ctx.lineWidth = 1; polygon(contour); ctx.stroke();
      label(ctx, stage === "aa" ? "coverage fringe: alpha falls across the outer band" : "centerline expanded into a stroke ribbon", 22, 30, stage === "aa" ? "#ff9a5c" : "#67e8f9");
    }
    points.forEach((p, i) => {
      ctx.fillStyle = i === 1 || i === 2 ? "#ff9a5c" : "#d9e7f2"; ctx.beginPath(); ctx.arc(p.x, p.y, i === 1 || i === 2 ? 9 : 6, 0, Math.PI * 2); ctx.fill();
      label(ctx, `P${i}`, p.x + 11, p.y - 10, i === 1 || i === 2 ? "#ffb085" : "#9eb0bf");
    });
    const triangles = fillPoly.length - 2;
    $("pathMetrics").innerHTML = metric("current stage", stage.toUpperCase())
      + metric("curve verbs", stage === "curve" ? "1 cubic" : "0", stage === "curve" ? "仍是参数曲线" : "flatten 后被点列替代")
      + metric("contour points", contour.length)
      + metric("teaching triangles", stage === "fill" ? triangles : "—", "实际 tessellator 会处理凹多边形与填充规则");
  }
  function pointer(event) {
    const rect = canvas.getBoundingClientRect();
    return { x: (event.clientX - rect.left) * canvas.width / rect.width, y: (event.clientY - rect.top) * canvas.height / rect.height };
  }
  canvas.addEventListener("pointerdown", (event) => {
    const p = pointer(event);
    dragging = [1, 2].find((i) => Math.hypot(points[i].x - p.x, points[i].y - p.y) < 28) ?? -1;
    if (dragging >= 0) canvas.setPointerCapture(event.pointerId);
  });
  canvas.addEventListener("pointermove", (event) => {
    if (dragging < 0) return;
    const p = pointer(event); points[dragging].x = clamp(p.x, 20, canvas.width - 20); points[dragging].y = clamp(p.y, 35, canvas.height - 40); render();
  });
  canvas.addEventListener("pointerup", () => { dragging = -1; });
  render();
}

// 05 — text ----------------------------------------------------------------
function initTextLab() {
  const canvas = $("textCanvas"), ctx = canvas.getContext("2d");
  let kind = "atlas";
  const getKind = setSegmented("textKind", (value) => { kind = value; render(); });
  ["textSize", "textDpr", "textOpaque", "textLayer", "textRotate"].forEach((id) => $(id).addEventListener("input", render));

  function render() {
    kind = getKind() || kind;
    const logical = +$("textSize").value, dpr = +$("textDpr").value;
    const raster = Math.min(512, Math.max(logical, Math.round(logical * dpr)));
    const drawScale = logical / raster;
    const opaque = $("textOpaque").checked, inLayer = $("textLayer").checked, rotated = $("textRotate").checked;
    const reasons = [];
    if (kind !== "bitmap") reasons.push("输出不是 bitmap run");
    if (!opaque) reasons.push("目标不是 opaque");
    if (inLayer) reasons.push("saveLayer 会改变合成目标");
    if (rotated) reasons.push("变换不是轴对齐");
    const clearType = reasons.length === 0;
    $("textSizeOut").textContent = `${logical}px`; $("textDprOut").textContent = `${dpr}×`;
    $("textKindBadge").textContent = kind === "atlas" ? "GLYPH ATLAS" : kind === "bitmap" ? "RUN BITMAP" : "GEOMETRY";

    grid(ctx, canvas.width, canvas.height, 40);
    ctx.save();
    ctx.translate(85, 190);
    if (rotated) ctx.rotate(-8 * Math.PI / 180);
    const visualSize = 72;
    ctx.font = `700 ${visualSize}px system-ui, sans-serif`;
    ctx.textBaseline = "alphabetic";
    const text = "Canvas 文字";
    const width = ctx.measureText(text).width;
    if (kind === "bitmap") {
      ctx.fillStyle = "rgba(255,154,92,.12)"; ctx.fillRect(-14, -visualSize, width + 28, visualSize + 28);
      ctx.strokeStyle = "#ff9a5c"; ctx.setLineDash([7, 5]); ctx.strokeRect(-14, -visualSize, width + 28, visualSize + 28); ctx.setLineDash([]);
      ctx.fillStyle = clearType ? "#f5f7fa" : "#c8d5df"; ctx.fillText(text, 0, 0);
      label(ctx, "one run bitmap / one textured quad", 0, 52, "#ffb085");
    } else if (kind === "atlas") {
      ctx.fillStyle = "#f5f7fa"; ctx.fillText(text, 0, 0);
      const glyphWidths = [54, 50, 51, 49, 49, 35, 72, 72];
      let cursor = -4;
      ctx.strokeStyle = "rgba(95,224,255,.72)"; ctx.lineWidth = 1;
      glyphWidths.forEach((gw, index) => {
        ctx.strokeRect(cursor, -visualSize + (index % 2), gw, visualSize + 10);
        ctx.fillStyle = "#5fe0ff"; ctx.fillRect(cursor + 3, -visualSize + 4, 4, 4); cursor += gw;
      });
      label(ctx, "glyph atlas: each glyph becomes a textured quad", 0, 52, "#67e8f9");
    } else {
      ctx.lineWidth = 2; ctx.strokeStyle = "#5fe0ff"; ctx.strokeText(text, 0, 0);
      ctx.strokeStyle = "rgba(255,154,92,.42)"; ctx.lineWidth = 1;
      for (let x = 0; x < width; x += 18) {
        ctx.beginPath(); ctx.moveTo(x, 0); ctx.lineTo(x + 11, -visualSize * .75); ctx.lineTo(x + 22, 0); ctx.stroke();
      }
      label(ctx, "geometry run: contours are tessellated", 0, 52, "#67e8f9");
    }
    ctx.restore();
    label(ctx, `logical ${logical}px`, 24, 30, "#9fb0ba");
    label(ctx, `raster ${raster}px`, 170, 30, "#ff9a5c");
    label(ctx, `draw scale ${crisp(drawScale, 3)}×`, 315, 30, "#67e8f9");
    ctx.strokeStyle = "rgba(255,255,255,.15)"; ctx.beginPath(); ctx.moveTo(22, 48); ctx.lineTo(795, 48); ctx.stroke();

    $("textEquation").innerHTML = `<b>${logical}px × ${dpr} = ${crisp(logical * dpr)}px</b><br>raster size = ${raster}px；绘制时缩回 <b>${crisp(drawScale, 3)}×</b>，所以布局仍按 ${logical}px 计算。`;
    $("clearTypeTrace").className = `trace-box ${clearType ? "good" : "bad"}`;
    $("clearTypeTrace").innerHTML = clearType
      ? '<b>ClearType 可以启用</b><br>bitmap run + opaque target + axis-aligned transform；其余混合前提在此实验中视为满足。'
      : `<b>ClearType 退回灰度抗锯齿</b><br>${reasons.map((reason) => `× ${reason}`).join("<br>")}`;
  }
  render();
}

// 06 — clip ----------------------------------------------------------------
function initClipLab() {
  const canvas = $("clipCanvas"), ctx = canvas.getContext("2d");
  const maskCanvas = $("maskCanvas"), maskCtx = maskCanvas.getContext("2d");
  let shape = "rect";
  const getShape = setSegmented("clipShape", (value) => { shape = value; render(); });
  $("clipLayers").addEventListener("input", render);

  function makeShape(context, type, layer, sx = 1, sy = 1) {
    const cx = (320 + layer * 23) * sx, cy = (215 - layer * 15) * sy;
    const w = (350 - layer * 28) * sx, h = (245 - layer * 12) * sy;
    context.beginPath();
    if (type === "rect") {
      context.rect((cx - w / 2), (cy - h / 2), w, h);
    } else if (type === "rotated") {
      const angle = (.16 + layer * .08);
      const corners = [[-w/2,-h/2],[w/2,-h/2],[w/2,h/2],[-w/2,h/2]].map(([x,y]) => ({x: cx + x*Math.cos(angle)-y*Math.sin(angle), y: cy+x*Math.sin(angle)+y*Math.cos(angle)}));
      corners.forEach((p, i) => i ? context.lineTo(p.x, p.y) : context.moveTo(p.x, p.y)); context.closePath();
    } else if (type === "circle") {
      context.ellipse(cx, cy, w / 2, h / 2, 0, 0, Math.PI * 2);
    } else {
      const outer = Math.min(w, h) / 2, inner = outer * .45;
      for (let i = 0; i < 10; i++) {
        const a = -Math.PI / 2 + i * Math.PI / 5, r = i % 2 ? inner : outer;
        const px = cx + Math.cos(a) * r, py = cy + Math.sin(a) * r;
        i ? context.lineTo(px, py) : context.moveTo(px, py);
      }
      context.closePath();
    }
  }

  function drawContent(context) {
    const gradient = context.createLinearGradient(0, 0, 640, 430);
    gradient.addColorStop(0, "#7357ff"); gradient.addColorStop(.5, "#19bfcf"); gradient.addColorStop(1, "#ff8552");
    context.fillStyle = gradient; context.fillRect(0, 0, 640, 430);
    context.fillStyle = "rgba(7,13,19,.28)";
    for (let y = 0; y < 430; y += 52) for (let x = 0; x < 640; x += 52) if ((x / 52 + y / 52) % 2) context.fillRect(x, y, 52, 52);
    context.fillStyle = "rgba(255,255,255,.9)"; context.font = "800 54px system-ui, sans-serif"; context.fillText("CLIP", 242, 232);
  }

  function render() {
    shape = getShape() || shape;
    const layers = +$("clipLayers").value; $("clipLayersOut").textContent = layers;
    grid(ctx, 640, 430, 40);
    ctx.save();
    for (let i = 0; i < layers; i++) { makeShape(ctx, shape, i); ctx.clip(); }
    drawContent(ctx); ctx.restore();
    ctx.save(); ctx.strokeStyle = "rgba(255,154,92,.8)"; ctx.setLineDash([8, 6]); ctx.lineWidth = 2;
    for (let i = 0; i < layers; i++) { makeShape(ctx, shape, i); ctx.stroke(); }
    ctx.restore(); label(ctx, "orange = submitted clip geometry / coarse bounds candidate", 15, 28, "#ffb085");

    const low = document.createElement("canvas"); low.width = low.height = 108;
    const lctx = low.getContext("2d"); lctx.fillStyle = "#000"; lctx.fillRect(0, 0, 108, 108);
    lctx.save();
    for (let i = 0; i < layers; i++) { makeShape(lctx, shape, i, 108/640, 108/430); lctx.clip(); }
    lctx.fillStyle = "#fff"; lctx.fillRect(0, 0, 108, 108); lctx.restore();
    maskCtx.fillStyle = "#071018"; maskCtx.fillRect(0, 0, 430, 430);
    maskCtx.imageSmoothingEnabled = false; maskCtx.drawImage(low, 0, 0, 430, 430);
    maskCtx.strokeStyle = "rgba(95,224,255,.12)"; maskCtx.lineWidth = 1;
    for (let n = 0; n <= 430; n += 430/18) { maskCtx.beginPath(); maskCtx.moveTo(n,0); maskCtx.lineTo(n,430); maskCtx.stroke(); maskCtx.beginPath(); maskCtx.moveTo(0,n); maskCtx.lineTo(430,n); maskCtx.stroke(); }
    label(maskCtx, shape === "rect" ? "no mask allocation in WhatsCanvas" : "white = 1 · black = 0 · edge gray = partial coverage", 12, 418, shape === "rect" ? "#89d185" : "#67e8f9");

    const scissor = shape === "rect";
    $("clipMetrics").innerHTML = metric("coarse reject", "integer bounds", "所有形状先缩小后续工作区域")
      + metric("exact clip", scissor ? "SCISSOR" : "COVERAGE MASK", scissor ? "轴对齐矩形直接求交" : "像素级保留边缘覆盖率")
      + metric("mask passes", scissor ? 0 : layers, scissor ? "多层矩形仍可折叠成一个矩形" : "逐层乘入 accumulator")
      + metric("mask resources", scissor ? 0 : 1, scissor ? "不分配纹理" : "在有效 bounds 内分配");
  }
  render();
}

// 07 — save / restore -------------------------------------------------------
function initStateLab() {
  const canvas = $("stateCanvas"), ctx = canvas.getContext("2d");
  const lines = [
    "// initial state",
    "canvas.save();",
    "canvas.translate(190, 70);",
    "canvas.clipRect({40, 40, 270, 210});",
    "canvas.drawRect(redRect, redPaint);",
    "canvas.restore();",
    "canvas.drawRect(cyanRect, cyanPaint);"
  ];
  $("stateStep").addEventListener("input", render);
  function drawRectWithState(color, transformed, clipped) {
    ctx.save();
    if (transformed) ctx.translate(190, 70);
    if (clipped) { ctx.beginPath(); ctx.rect(40, 40, 270, 210); ctx.clip(); }
    ctx.fillStyle = color; ctx.fillRect(0, 0, 350, 270);
    ctx.strokeStyle = "rgba(255,255,255,.55)"; ctx.lineWidth = 2; ctx.strokeRect(0, 0, 350, 270);
    ctx.restore();
  }
  function render() {
    const step = +$("stateStep").value; $("stateStepLabel").textContent = `STEP ${step} / 6`;
    grid(ctx, 760, 460, 40);
    if (step >= 4) drawRectWithState("rgba(255,91,91,.68)", true, true);
    if (step >= 6) drawRectWithState("rgba(49,215,229,.64)", false, false);
    if (step >= 2 && step <= 4) {
      ctx.save(); ctx.translate(190,70); ctx.strokeStyle = "#ff9a5c"; ctx.setLineDash([8,5]); ctx.strokeRect(0,0,350,270);
      if (step >= 3) { ctx.strokeStyle = "#fff"; ctx.setLineDash([3,4]); ctx.strokeRect(40,40,270,210); label(ctx,"active clip",47,65,"#fff"); }
      ctx.restore();
    }
    ctx.strokeStyle = "#5fe0ff"; ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(20, 410); ctx.lineTo(740, 410); ctx.stroke();
    label(ctx, step >= 5 ? "current origin restored to (0, 0)" : step >= 2 ? "current origin = (190, 70)" : "current origin = (0, 0)", 25, 438, step >= 5 ? "#89d185" : "#67e8f9");
    $("stateCode").innerHTML = `<code>${lines.map((line, i) => `<span class="${i === step ? "active-line" : ""}">${String(i).padStart(2,"0")}  ${line}</span>`).join("")}</code>`;
    const restored = step >= 5;
    const saveCount = step >= 1 && step < 5 ? 2 : 1;
    const commands = (step >= 4 ? 1 : 0) + (step >= 6 ? 1 : 0);
    $("stateMetrics").innerHTML = metric("save count", saveCount)
      + metric("current matrix", step >= 2 && !restored ? "T(190, 70)" : "IDENTITY")
      + metric("current clip", step >= 3 && !restored ? "270 × 210" : "NONE")
      + metric("recorded draws", commands, "restore 只改当前状态，不删除命令");
    $("stateTrace").className = `trace-box ${restored ? "good" : ""}`;
    $("stateTrace").innerHTML = step === 5
      ? '<b>状态已恢复，红色仍然存在。</b><br>红色矩形在 drawRect() 时已经携带当时的 matrix 与 clip 被记录。'
      : step === 6
        ? '<b>青色矩形使用恢复后的状态。</b><br>它没有继承红色矩形的 translate 和 clip。'
        : '<b>拖动时间线。</b><br>留意“当前状态”和“已经录制的绘制”是两件事。';
  }
  render();
}

// 08 — saveLayer ------------------------------------------------------------
function initLayerLab() {
  const direct = $("directAlphaCanvas"), dctx = direct.getContext("2d", { willReadFrequently: true });
  const layered = $("layerAlphaCanvas"), lctx = layered.getContext("2d", { willReadFrequently: true });
  ["layerAlpha", "layerBounds", "layerPasses"].forEach((id) => $(id).addEventListener("input", render));
  const shapes = [
    { color: "#ff685f", x: 110, y: 90, w: 260, h: 200, r: 48 },
    { color: "#5fe0ff", x: 225, y: 125, w: 250, h: 210, r: 105 },
    { color: "#8c72ff", x: 155, y: 205, w: 280, h: 150, r: 28 }
  ];
  function background(context) { grid(context, 580, 430, 36); context.fillStyle = "#162332"; context.fillRect(45, 48, 490, 330); }
  function paintShapes(context, alpha) {
    shapes.forEach((shape) => { context.globalAlpha = alpha; roundedPath(context, shape.x, shape.y, shape.w, shape.h, shape.r); context.fillStyle = shape.color; context.fill(); });
    context.globalAlpha = 1;
  }
  function rgba(context, x, y) { const p = context.getImageData(x, y, 1, 1).data; return `rgb(${p[0]}, ${p[1]}, ${p[2]})`; }
  function render() {
    const alpha = +$("layerAlpha").value, boundsScale = +$("layerBounds").value, passes = +$("layerPasses").value;
    $("layerAlphaOut").textContent = alpha.toFixed(2); $("layerBoundsOut").textContent = `${Math.round(boundsScale * 100)}%`; $("layerPassesOut").textContent = passes;
    background(dctx); paintShapes(dctx, alpha);
    const off = document.createElement("canvas"); off.width = 580; off.height = 430; const octx = off.getContext("2d");
    const bw = 490 * boundsScale, bh = 330 * boundsScale, bx = 45 + (490 - bw) / 2, by = 48 + (330 - bh) / 2;
    octx.save(); octx.beginPath(); octx.rect(bx, by, bw, bh); octx.clip(); paintShapes(octx, 1); octx.restore();
    background(lctx); lctx.globalAlpha = alpha; lctx.drawImage(off, 0, 0); lctx.globalAlpha = 1;
    lctx.strokeStyle = boundsScale < .99 ? "#ff9a5c" : "#5fe0ff"; lctx.lineWidth = 2; lctx.setLineDash([8,5]); lctx.strokeRect(bx,by,bw,bh); lctx.setLineDash([]);
    label(dctx, "same alpha applied 3 times in overlaps", 18, 26, "#ffb085");
    label(lctx, boundsScale < .99 ? "orange bounds crop the offscreen target" : "full offscreen bounds", 18, 26, boundsScale < .99 ? "#ffb085" : "#67e8f9");
    const directPixel = rgba(dctx, 290, 245), layerPixel = rgba(lctx, 290, 245);
    const pixelCost = Math.round(bw * bh * passes).toLocaleString("en-US");
    $("layerMetrics").innerHTML = metric("overlap pixel / direct", directPixel, "每个图元各混合一次")
      + metric("overlap pixel / layer", layerPixel, "组内先合成，再整体乘 alpha")
      + metric("offscreen pixels", pixelCost, `${Math.round(bw)} × ${Math.round(bh)} × ${passes} pass`)
      + metric("command rewrite", "BEGIN → draws → END", "范围内命令改写到离屏目标");
    $("layerTrace").className = `trace-box ${boundsScale < .75 ? "bad" : "good"}`;
    $("layerTrace").innerHTML = boundsScale < .75
      ? '<b>Bounds 已经裁掉内容。</b><br>saveLayer 的 bounds 既影响内存，也决定哪些像素有机会进入离屏目标。'
      : `<b>离屏代价随面积和 pass 数增长。</b><br>当前约 ${pixelCost} pixel-visits；alpha 只在 END_LAYER 合成时应用一次。`;
  }
  render();
}

initFrameLab();
initRoundRectLab();
initImageLab();
initPathLab();
initTextLab();
initClipLab();
initStateLab();
initLayerLab();
