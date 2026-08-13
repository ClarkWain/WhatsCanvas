from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import shutil
import subprocess
from typing import Iterable

from PIL import Image, ImageChops, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "images" / "canvas-architecture.png"


def resolve_font() -> str:
    override = os.environ.get("WHATSCANVAS_DIAGRAM_FONT")
    if override:
        path = Path(override).expanduser()
        if path.is_file():
            return str(path)
        raise FileNotFoundError(f"WHATSCANVAS_DIAGRAM_FONT does not exist: {path}")

    if os.name == "nt":
        import winreg

        registry_key = r"SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts"
        preferred_names = ("yahei", "dengxian", "simhei", "cjk")
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, registry_key) as fonts:
            values = [winreg.EnumValue(fonts, index)[:2]
                      for index in range(winreg.QueryInfoKey(fonts)[1])]
        windows_root = os.environ.get("WINDIR")
        for preferred in preferred_names:
            for display_name, registered_path in values:
                if preferred not in display_name.casefold():
                    continue
                path = Path(registered_path)
                if not path.is_absolute() and windows_root:
                    path = Path(windows_root) / "Fonts" / path
                if path.is_file():
                    return str(path)

    fontconfig = shutil.which("fc-match")
    if fontconfig:
        result = subprocess.run(
            [fontconfig, "-f", "%{file}", "sans-serif:lang=zh-cn"],
            check=True,
            capture_output=True,
            text=True,
        )
        path = Path(result.stdout.strip())
        if path.is_file():
            return str(path)

    raise RuntimeError(
        "No CJK font was discovered; set WHATSCANVAS_DIAGRAM_FONT to a font file."
    )


FONT = resolve_font()


@dataclass(frozen=True)
class Box:
    name: str
    rect: tuple[int, int, int, int]
    fill: str
    stroke: str
    title: str
    path: str
    lines: tuple[str, ...]


def font(size: int) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(FONT, size)


TITLE = font(38)
SUBTITLE = font(20)
LAYER = font(18)
BOX_TITLE = font(24)
BOX_PATH = font(17)
BOX_TEXT = font(18)
SMALL = font(16)


def text_size(draw: ImageDraw.ImageDraw, text: str, fnt: ImageFont.FreeTypeFont) -> tuple[int, int]:
    box = draw.textbbox((0, 0), text, font=fnt)
    return box[2] - box[0], box[3] - box[1]


def centered_text(draw: ImageDraw.ImageDraw, xy: tuple[int, int], text: str, fnt: ImageFont.FreeTypeFont, fill: str) -> None:
    x, y = xy
    w, _ = text_size(draw, text, fnt)
    draw.text((x - w / 2, y), text, font=fnt, fill=fill)


def fit_line(draw: ImageDraw.ImageDraw, text: str, max_width: int, fnt: ImageFont.FreeTypeFont) -> str:
    if text_size(draw, text, fnt)[0] <= max_width:
        return text
    return text


def draw_box(draw: ImageDraw.ImageDraw, box: Box, errors: list[str]) -> None:
    x, y, w, h = box.rect
    draw.rounded_rectangle((x, y, x + w, y + h), radius=14, fill=box.fill, outline=box.stroke, width=3)

    horizontal_padding = 52
    vertical_padding = 26
    title = fit_line(draw, box.title, w - horizontal_padding, BOX_TITLE)
    path = fit_line(draw, box.path, w - horizontal_padding, BOX_PATH)
    body = [fit_line(draw, line, w - horizontal_padding, BOX_TEXT) for line in box.lines]

    total_h = 28 + 22 + len(body) * 22
    cy = y + (h - total_h) // 2
    centered_text(draw, (x + w // 2, cy), title, BOX_TITLE, "#0f172a")
    cy += 31
    centered_text(draw, (x + w // 2, cy), path, BOX_PATH, "#475569")
    cy += 26
    for line in body:
        centered_text(draw, (x + w // 2, cy), line, BOX_TEXT, "#1e293b")
        cy += 22

    for line, fnt in [(title, BOX_TITLE), (path, BOX_PATH), *[(line, BOX_TEXT) for line in body]]:
        if text_size(draw, line, fnt)[0] > w - horizontal_padding:
            errors.append(f"text overflow in {box.name}: {line}")
    if total_h > h - vertical_padding:
        errors.append(f"text height overflow in {box.name}")


def draw_arrow(draw: ImageDraw.ImageDraw, start: tuple[int, int], end: tuple[int, int], color: str = "#334155") -> None:
    draw.line((start, end), fill=color, width=3)
    sx, sy = start
    ex, ey = end
    if abs(ex - sx) < abs(ey - sy):
        pts = [(ex, ey), (ex - 8, ey - 14), (ex + 8, ey - 14)]
    else:
        pts = [(ex, ey), (ex - 14, ey - 8), (ex - 14, ey + 8)]
    draw.polygon(pts, fill=color)


def intersects(a: tuple[int, int, int, int], b: tuple[int, int, int, int]) -> bool:
    ax, ay, aw, ah = a
    bx, by, bw, bh = b
    return ax < bx + bw and ax + aw > bx and ay < by + bh and ay + ah > by


def validate(boxes: Iterable[Box], boundary: tuple[int, int, int, int]) -> list[str]:
    errors: list[str] = []
    items = list(boxes)
    for i, a in enumerate(items):
        for b in items[i + 1 :]:
            if intersects(a.rect, b.rect):
                errors.append(f"box overlap: {a.name} / {b.name}")
    bx, by, bw, bh = boundary
    for item in items:
        if item.name in {"app", "validation", "third_party", "gpu"}:
            continue
        x, y, w, h = item.rect
        if x < bx or y < by or x + w > bx + bw or y + h > by + bh:
            errors.append(f"box outside library boundary: {item.name}")
    return errors


def main() -> None:
    img = Image.new("RGB", (1800, 1360), "#f8fafc")
    draw = ImageDraw.Draw(img)
    errors: list[str] = []

    centered_text(draw, (900, 42), "WhatsCanvas 当前 Canvas 架构图", TITLE, "#0f172a")
    centered_text(
        draw,
        (900, 92),
        "当前事实：核心 canvas / text / command / render 后端无关；GL-family（WhatsCanvasOpenGL/ES）内含可选 Vulkan，另有独立的纯 CPU WhatsCanvas::Software 目标；对外暴露 include/wsc",
        SUBTITLE,
        "#475569",
    )

    external = [
        Box("app", (110, 130, 340, 112), "#e2e8f0", "#64748b", "应用 / 示例", "showcase / games", ("调用公共 Canvas API",)),
        Box("validation", (520, 130, 340, 112), "#e2e8f0", "#64748b", "验证入口", "tests / scripts / benchmarks", ("构建、冒烟、回归验证",)),
        Box("third_party", (940, 130, 340, 112), "#e2e8f0", "#64748b", "第三方依赖", "GLAD / GLM / STB", ("加载、数学、图像",)),
        Box("gpu", (1360, 130, 340, 112), "#e2e8f0", "#64748b", "OpenGL / GPU", "driver and GPU resources", ("纹理、FBO、stencil、readback",)),
    ]

    boundary = (85, 285, 1630, 1020)
    bx, by, bw, bh = boundary
    draw.rounded_rectangle((bx, by, bx + bw, by + bh), radius=24, fill="#ffffff", outline="#94a3b8", width=3)
    draw.rounded_rectangle((bx, by, bx + bw, by + bh), radius=24, outline="#94a3b8", width=1)
    draw.text((125, 320), "WhatsCanvas GL-family library", font=font(28), fill="#1e293b")
    draw.text((125, 356), "编译边界来自 cmake/WhatsCanvasOpenGL.cmake", font=SMALL, fill="#475569")

    boxes = [
        Box("api", (310, 392, 1180, 124), "#dbeafe", "#2563eb", "公共 API 层", "include/wsc", ("Canvas / Paint / Path / Image / Matrix / Color / TextureSource",)),
        Box("canvas_core", (310, 590, 300, 146), "#dcfce7", "#16a34a", "Canvas Core", "src/canvas/Canvas.cpp", ("Canvas::Impl / PIMPL", "绘制规划 / 帧控制 / saveLayer")),
        Box("state", (645, 590, 300, 146), "#dcfce7", "#16a34a", "Graphics State", "src/render/GraphicsState*", ("matrix / clip / save", "clipPath / hit-test 状态")),
        Box("model", (980, 590, 300, 146), "#dcfce7", "#16a34a", "Canvas Data Model", "Paint / Path / Image / Matrix", ("样式 / 路径 / 纹理源", "Canvas/Image 可作纹理源")),
        Box("text", (1315, 590, 300, 146), "#dcfce7", "#16a34a", "Text Backend", "src/text", ("ITextBackend", "Basic / Native / Utils")),
        Box("command", (310, 815, 1180, 124), "#ffedd5", "#f97316", "命令录制层", "src/command", ("DrawData + DrawCommand", "Points / Lines / Path / Image / Text")),
        Box("renderer", (310, 995, 570, 144), "#ede9fe", "#7c3aed", "Renderer", "src/render/Renderer.*", ("命令队列 / 路径合批 / flush", "readPixels / offscreen")),
        Box("device", (980, 995, 510, 144), "#ede9fe", "#7c3aed", "Render Device Boundary", "src/render", ("RenderContext / IRenderDevice", "IRenderTarget / 状态应用")),
        Box("opengl", (310, 1185, 1180, 104), "#fee2e2", "#dc2626", "渲染后端（运行时可选择）", "OpenGLRenderDevice · VulkanRenderDevice · SoftwareRenderer", ("OpenGL / OpenGLES · 可选 Vulkan（离屏）· 纯 CPU 软件后端（零 GPU）",)),
    ]

    all_boxes = external + boxes
    errors.extend(validate(all_boxes, boundary))

    for b in external:
        draw_box(draw, b, errors)
    for b in boxes:
        draw_box(draw, b, errors)

    for y, label in [(445, "Public API"), (665, "Canvas Model"), (880, "Recording"), (1065, "Render Abstraction"), (1235, "Backend")]:
        draw.text((130, y), label, font=LAYER, fill="#475569")

    draw_arrow(draw, (900, 516), (900, 590))
    draw_arrow(draw, (900, 736), (900, 815))
    draw_arrow(draw, (900, 939), (900, 995))
    draw_arrow(draw, (900, 1139), (900, 1185))
    draw_arrow(draw, (880, 1067), (980, 1067), "#64748b")

    draw.text((895, 1045), "device API", font=SMALL, fill="#475569")
    if ImageChops.difference(img, Image.new("RGB", img.size, "#f8fafc")).getbbox() is None:
        errors.append("image appears blank")

    if errors:
        raise SystemExit("architecture diagram validation failed:\n" + "\n".join(errors))

    OUT.parent.mkdir(parents=True, exist_ok=True)
    img.save(OUT)
    print(f"wrote {OUT}")
    print("validation: PASS")


if __name__ == "__main__":
    main()
