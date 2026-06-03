/* ============================================================
   全站共享脚本：导航、流程图交互、滚动高亮
   ============================================================ */

// 章节清单（供导航与上一/下一章使用）
const CHAPTERS = [
  { id: "index",   num: "00", file: "index.html",                    title: "总览" },
  { id: "ch1",     num: "01", file: "chapters/ch1-history.html",     title: "字体简史" },
  { id: "ch2",     num: "02", file: "chapters/ch2-font-file.html",   title: "字体文件" },
  { id: "ch3",     num: "03", file: "chapters/ch3-cmap.html",        title: "字符到字形" },
  { id: "ch4",     num: "04", file: "chapters/ch4-shaping.html",     title: "文本塑形" },
  { id: "ch5",     num: "05", file: "chapters/ch5-metrics.html",     title: "测量与排版" },
  { id: "ch6",     num: "06", file: "chapters/ch6-rasterization.html",title: "栅格化" },
  { id: "ch7",     num: "07", file: "chapters/ch7-libraries.html",   title: "底层库协作" },
  { id: "ch8",     num: "08", file: "chapters/ch8-gpu.html",         title: "GPU 绘制" },
  { id: "ch9",     num: "09", file: "chapters/ch9-webfonts.html",    title: "Web 字体" },
  { id: "ch10",    num: "10", file: "chapters/ch10-libraries-code.html", title: "三大库代码示例" },
  { id: "ch11",    num: "11", file: "chapters/ch11-sdf.html", title: "SDF / MSDF" },
];

// 渲染顶部导航
function buildNav(activeId, base = "") {
  const nav = document.querySelector(".nav-links");
  if (!nav) return;
  nav.innerHTML = CHAPTERS.map(c => {
    const href = base + c.file;
    const cls = c.id === activeId ? "active" : "";
    const label = c.id === "index" ? "总览" : c.num;
    return `<a href="${href}" class="${cls}" title="${c.title}">${label}</a>`;
  }).join("");
}

// 渲染章节底部上一章/下一章
function buildFooterNav(activeId, base = "") {
  const foot = document.querySelector(".chapter-foot");
  if (!foot) return;
  const idx = CHAPTERS.findIndex(c => c.id === activeId);
  const prev = CHAPTERS[idx - 1];
  const next = CHAPTERS[idx + 1];
  let html = "";
  if (prev) {
    html += `<a class="prev" href="${base + prev.file}">
      <div class="dir">← 上一章 · ${prev.num}</div>
      <div class="nm">${prev.title}</div></a>`;
  } else { html += `<span style="flex:1"></span>`; }
  if (next) {
    html += `<a class="next" href="${base + next.file}">
      <div class="dir">下一章 · ${next.num} →</div>
      <div class="nm">${next.title}</div></a>`;
  }
  foot.innerHTML = html;
}

// 通用：把 range 的值实时显示到 label .val
function bindRange(input, fmt = (v) => v) {
  const label = input.closest(".control")?.querySelector(".val");
  const update = () => { if (label) label.textContent = fmt(input.value); };
  input.addEventListener("input", update);
  update();
}

document.addEventListener("DOMContentLoaded", () => {
  const active = document.body.dataset.page || "index";
  const base = document.body.dataset.base || "";
  buildNav(active, base);
  buildFooterNav(active, base);
});
