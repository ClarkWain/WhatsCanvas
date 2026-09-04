# 第一章：环境搭建与第一帧

> 本章目标：从零开始配置 WhatsCanvas 开发环境，编写第一个程序，离屏渲染一帧并输出图片文件。

---

## 1.1 WhatsCanvas 是什么

WhatsCanvas 是一个 C++17 编写的 2D 渲染库，定位介于 NanoVG（轻量但功能有限）和 Skia（强大但体量庞大）之间。它提供类似 HTML Canvas 的 API 风格：

- **Canvas** —— 绘制表面，管理帧的开始/结束
- **Paint** —— 画笔属性（颜色、渐变、描边等）
- **Path** —— 2D 几何路径

支持 5 种渲染后端：Software（纯 CPU）、OpenGL、OpenGL ES、Vulkan、Metal。

---

## 1.2 获取 WhatsCanvas

有三种主要方式：

### 方式一：GitHub Release 预编译包（推荐新手）

从 [Releases](https://github.com/ClarkWain/WhatsCanvas/releases) 下载对应平台的包：

**桌面端：**

- Windows: `whatscanvas-win64-release-1.1.0.zip`
- Linux: `whatscanvas-linux-x64-release-1.1.0.zip`
- macOS: `whatscanvas-macos-universal-release-1.1.0.zip`

桌面包解压后目录结构：

```
whatscanvas-win64-release-1.1.0/
├── include/wsc/          # 头文件
├── lib/                  # 静态/动态库
├── bin/                  # DLL（Windows shared 构建）
└── lib/cmake/WhatsCanvas/ # CMake 配置文件
```

**移动端：**

- Android: `whatscanvas-android-release-1.1.0.aar`
  - Prefab AAR，内含公开头文件和 `armeabi-v7a`、`arm64-v8a`、`x86_64` 三套 OpenGL ES 库
  - 通过 Gradle 引入，详见 [Android 接入指南](../../platforms/ANDROID_INTEGRATION.md)
- iOS: `whatscanvas-ios-release-1.1.0.zip`
  - 静态 Metal/CoreText XCFramework，包含 `arm64` 真机切片和 `arm64`/`x86_64` 模拟器切片
  - 拖入 Xcode 项目 Frameworks 使用，详见 [iOS Build Notes](../../platforms/IOS_BUILD_NOTES.md)

### 方式二：从源码构建

```bash
git clone --recursive https://github.com/ClarkWain/WhatsCanvas.git
cd WhatsCanvas

# Windows (VS 2022)
build.bat --release --package --no-run

# Linux / macOS
sh ./build.sh --release --package --no-run
```

构建完成后，安装包位于 `out/package/Release/`。

---

## 1.3 创建第一个项目

创建如下项目结构：

```
my_first_wsc/
├── CMakeLists.txt
└── main.cpp
```

### CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.16)
project(MyFirstWSC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 WhatsCanvas
find_package(WhatsCanvas 1.1.0 CONFIG REQUIRED)

add_executable(MyFirstWSC main.cpp)
target_link_libraries(MyFirstWSC PRIVATE WhatsCanvas::Software)
```

### main.cpp

```cpp
#include <wsc/wsc.h>

int main()
{
    // 1. 创建 Canvas：使用 Software 后端，大小 256x256
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 256, 256);
    if (!canvas || !canvas->initializeContext()) {
        return 1;
    }

    // 2. 开始一帧
    canvas->beginFrame();

    // 3. 创建画笔：蓝色填充
    wsc::Paint fill;
    fill.setColor(wsc::Color(40, 120, 240, 255));  // RGBA
    fill.setAntiAlias(true);

    // 4. 绘制圆角矩形
    canvas->drawRoundRect(wsc::RectF(40, 40, 176, 176), 24.0f, fill);

    // 5. 结束帧
    canvas->endFrame();

    // 6. 输出到 PPM 文件
    return canvas->savePixelsPPM("first.ppm") ? 0 : 1;
}
```

---

## 1.4 编译与运行

```bash
# 配置（将 CMAKE_PREFIX_PATH 指向你的 WhatsCanvas 安装目录）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/whatscanvas-win64-release-1.1.0

# 编译
cmake --build build --config Release

# 运行
./build/Release/MyFirstWSC    # Windows
./build/MyFirstWSC            # Linux/macOS
```

运行后当前目录会生成 `first.ppm`，用支持 PPM 格式的图片查看器打开即可看到一个蓝色圆角矩形。

> **提示**：Windows 上如果使用的是 shared 构建的预编译包，运行前需将 `bin/` 目录加入 PATH，或将 DLL 复制到 exe 旁边：
> ```bat
> set "PATH=C:\path\to\whatscanvas\bin;%PATH%"
> build\Release\MyFirstWSC.exe
> ```

---

## 1.5 代码解析

让我们逐行理解这 24 行代码：

| 步骤 | 代码 | 说明 |
|:----:|------|------|
| 1 | `Canvas::create(Backend::Software, 256, 256)` | 创建一个 256x256 的离屏 Canvas，使用纯 CPU 渲染 |
| 2 | `canvas->initializeContext()` | 初始化渲染上下文（Software 后端无需 GPU） |
| 3 | `canvas->beginFrame()` | 开始录制一帧的绘制命令 |
| 4 | `fill.setColor(...)` | 设置画笔颜色为蓝色 (R=40, G=120, B=240, A=255) |
| 5 | `fill.setAntiAlias(true)` | 启用抗锯齿，边缘更平滑 |
| 6 | `drawRoundRect(RectF, radius, paint)` | 绘制圆角矩形，圆角半径 24px |
| 7 | `canvas->endFrame()` | 结束帧，执行所有绘制命令 |
| 8 | `savePixelsPPM("first.ppm")` | 将像素回读并保存为 PPM 格式 |

### 帧的生命周期

WhatsCanvas 的绘制遵循 **帧循环** 模式：

```
beginFrame() → 绘制命令 → endFrame() → [present() 或 readPixels()]
```

- **离屏渲染**（本章）：`endFrame()` 之后用 `savePixelsPPM()` 或 `readPixelsRGBA()` 获取像素
- **窗口渲染**（第 9 章）：`endFrame()` 之后调用 `present()` 显示到屏幕

---

## 1.6 关于 Software 后端

Software 后端是 WhatsCanvas 的 CPU 参考实现：

- **无需 GPU**：不依赖 OpenGL / Vulkan / Metal
- **确定性输出**：相同输入在不同机器上产生相同像素
- **适用场景**：单元测试、CI 环境、离屏图片生成、截图对比
- **限制**：性能低于 GPU 后端，不适合实时渲染大量内容

对于学习和验证来说，Software 后端是最佳起点。

---

## 1.7 小结

本章完成了：

- [x] 了解 WhatsCanvas 的定位和核心概念
- [x] 获取并配置 WhatsCanvas 库
- [x] 编写第一个程序并成功渲染
- [x] 理解帧生命周期

**下一章**：[基础图形绘制](./02-basic-shapes.md) —— 学习绘制矩形、圆、线段等各种基础图形。
