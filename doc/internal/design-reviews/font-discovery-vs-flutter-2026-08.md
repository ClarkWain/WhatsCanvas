# WhatsCanvas 与 Flutter 字体发现逻辑差距分析

> 维护者设计审查，记录 2026 年 8 月的对比环境与阶段性判断，不作为当前
> 公共能力说明。当前用户契约以
> `doc/public/guides/text/TEXT_FEATURE_MATRIX.md` 为准。

状态：Android provider/resolver、variable font 与固定 Skia discovery/matching/raster 门禁实施中（2026-08-17）
对比对象：`E:\Team_Bass\WhatsCanvas` 当前工作树、`G:\flutter-master` 当前工作树

## 1. 结论先行

WhatsCanvas 已从“先枚举系统字体再注册 alias”的单一路线演进为混合架构：公共
`FontResolver` 组合有优先级的 provider，Android API 29+ 通过 `AFontMatcher`
按 cluster/locale/style 查询，API 21-28 由系统 font config provider 匹配；原有
discovery snapshot 和显式 fallback chain 仍作为可审计、可移植的来源。Flutter
engine 则把动态字体、应用资产字体、测试字体和平台默认 `SkFontMgr` 组合成一个
`FontCollection`，在匹配 family/style/character 时按需查询。

两者并非简单的强弱关系：

- WhatsCanvas 的优势是发现结果可审计、`path + faceIndex` 明确、portable raster 路径可控，并且已经有进程级快照和 refresh 语义。
- Flutter 的优势是把“系统字体发现”和“字体选择”解耦，不要求先枚举所有系统字体；平台字体管理器负责按 family、style、字符和 locale 做按需匹配，应用字体则通过独立 provider 注入。
- WhatsCanvas 最值得借鉴的不是直接复制 Flutter/Skia 代码，而是其字体管理器分层、按字符匹配接口、资产字体懒加载、provider 优先级和缓存失效边界。
- 当前最大的结构性差距已转向平台覆盖面：Android provider、asset/dynamic
  懒加载和 Web 下载调度核心已落地；iOS CoreText、Linux fontconfig、浏览器
  fetch/FontFaceSet glue 与 host relayout 通知仍待实现。

建议优先级：

1. P0：抽象 `FontProvider`/`FontResolver`，把“发现”“注册”“按字符匹配”“fallback”分开。
2. P0：为 portable 路径增加 `matchFamilyStyleCharacter(family, weight, slant, locale, codepoint/cluster)` 语义，发现阶段不再承担全部覆盖判断。
3. 已完成：`refreshSystemFonts()` 同时失效 provider、shape/layout/face、
   loaded-face、atlas 与 native bitmap/measure cache，避免同路径替换后复用旧数据。
4. P1：保留现有 discovery snapshot，但补充真实 family、style、stretch/variable axis、来源和 `faceIndex` 元数据。
5. 已完成：应用/动态字体 provider 级懒加载、Web host-driven 下载状态机，以及精确的 family-cache / render-cache 失效。
6. P1：增加按脚本、locale、emoji/color font、TTC/OTC 和新安装字体的验证矩阵。

在上述基础上，补充三条会影响 iOS/Web 的架构结论：

- “发现系统有哪些字体”和“为一个字符簇匹配字体”必须是不同能力；正常渲染不应依赖全量 discovery 成功。
- `FontFace` 目前只有文件/内存来源，后续必须能表达平台内部字体或流式 source，否则 Windows 云字体、Apple 私有系统字体和 Web 下载字体会被文件路径模型限制。
- 字体刷新必须形成 `provider generation -> resolution/shape/layout/raster cache -> host relayout` 的完整失效链，不能只清一个 backend 内部缓存。

## 2. 术语和范围

本文区分三个概念：

| 概念 | 含义 | WhatsCanvas | Flutter |
| --- | --- | --- | --- |
| 字体发现（discovery） | 从平台或资产来源得到字体 family/face/file 的记录 | 有独立的系统 face 枚举器；Android 示例是有限字符探测 | 平台 `SkFontMgr` 负责系统查询；资产由 provider 注册，通常不做应用层全量枚举 |
| 字体注册（registration） | 把文件、内存数据或 `SkTypeface` 纳入可匹配集合 | `FontFace::fromFile/fromMemory` + `FontManager` | asset manager、dynamic manager、`SkTypeface` provider |
| fallback | 首选字体不覆盖目标字符时的后备选择 | portable 路径显式 chain + glyph/cluster 检查；DirectWrite 可接系统 resolver | SkParagraph/SkFontMgr 按 family/style/character/locale 做平台匹配 |
| family alias | 应用为了稳定选择而创建的逻辑名称 | `WhatsCanvas Sans`、`WhatsCanvas CJK` 等 | 应用 manifest family、动态注册 family；系统 family 通常保留平台真实名称 |
| face identity | 同一 source 中的具体字体实例 | source + `faceIndex` + 规范化 variation axes；provider 另加 family/style | `SkTypeface`/font manager 内部 identity；资产路径由 provider 管理 |

本文讨论字体记录发现、注册和 fallback，不把它们等同于完整 Unicode glyph coverage。一个字体文件被发现或注册成功，只能证明它可被加载或索引，不能证明它覆盖某个脚本、emoji 序列或所有变体选择器。

## 3. WhatsCanvas 当前实现

### 3.1 系统字体发现链路

入口在 [`SystemFontEnumerator.h`](../../../src/text/SystemFontEnumerator.h) 和 [`SystemFontEnumerator.cpp`](../../../src/text/SystemFontEnumerator.cpp)：

1. 平台层枚举字体 face：
   - macOS：CoreText family/descriptor，并解析文件 URL 与 TTC/OTC/dfont face index。
   - Windows：DirectWrite `GetSystemFontCollection`，遍历 family、font、font face 和文件引用。
   - Linux：fontconfig `FcFontList`，读取 family、file、index、weight、slant。
2. 只有能转换为本地文件路径的记录才能变成 `FontFace::fromFile`。Windows 云字体或没有稳定本地路径的字体会被跳过；因此“已安装字体”在这里实际是“平台 API 能报告且 WhatsCanvas 能表示的字体 face”。
3. `FontSystem::discoverInstalledFontFaces()` 以 `family + path + faceIndex + weight + italic` 去重，并建立进程级 discovery snapshot。
4. snapshot 有 generation；首次建立从非零 generation 开始，`refreshInstalledFonts()` 重新枚举并递增 generation。

关键证据：

- [`SystemFontEnumerator.cpp:312-373`](../../../src/text/SystemFontEnumerator.cpp) 的 Windows 枚举；`localFontFilePath()` 对无稳定路径字体返回空值。
- [`SystemFontEnumerator.cpp:378-425`](../../../src/text/SystemFontEnumerator.cpp) 的 fontconfig 枚举与权重归一化。
- [`SystemFontEnumerator.cpp:525-553`](../../../src/text/SystemFontEnumerator.cpp) 的 snapshot、去重和 generation。
- [`Font.h:317-368`](../../../include/wsc/Font.h) 的 refresh/discovery API 契约。

### 3.2 从真实 family 到逻辑 alias

`FontSystem::defaultSystemFontFaces()` 不直接把所有真实 family 作为默认 fallback，而是从候选 family 中选出 face，再生成稳定的逻辑 alias：

- `WhatsCanvas Sans`
- `WhatsCanvas CJK`
- `WhatsCanvas Arabic`
- `WhatsCanvas Hebrew`
- `WhatsCanvas Symbol`
- `WhatsCanvas Serif`
- `WhatsCanvas Mono`

候选 family 按平台硬编码。例如 Windows 使用 Segoe UI、Microsoft YaHei、Segoe UI Symbol、Georgia、Consolas 等；Linux 使用 DejaVu/Noto/Liberation 系列；macOS 使用系统 UI、PingFang、Menlo 等。Latin、CJK、Arabic、Hebrew、Symbol alias 还附加了近似的 Unicode ranges。

这套设计解决了应用默认字体名的稳定性问题，但它有两个边界：

- alias 是应用策略，不是真实系统 family；调试和字体选择器必须同时保留二者。
- Unicode range 是选择提示，不是从字体 cmap 得到的真实覆盖集合。代码仍需在最终选择时查询 glyph/cluster。

关键证据：[`SystemFontEnumerator.cpp:462-496`](../../../src/text/SystemFontEnumerator.cpp)、[`SystemFontEnumerator.cpp:556-623`](../../../src/text/SystemFontEnumerator.cpp)。

### 3.3 FontManager 和 portable fallback

[`Font.h:180-297`](../../../include/wsc/Font.h) 中的 `FontManager`：

- 按 family 保存多个 `FontFace`。
- `findBestFace()` 先按 slant 惩罚，再按 weight 距离选择 face。
- `FontFallbackChain` 只保存有序 family 名称，不保存每个 Unicode range 的字体覆盖证明。
- `FontFace` 支持文件和内存来源，支持显式 `faceIndex` 和可选 codepoint ranges。

portable backend 在 [`BasicTextBackend.cpp:548-565`](../../../src/text/BasicTextBackend.cpp) 注册默认系统 alias 和默认 chain；在 [`BasicTextBackend.cpp:569-659`](../../../src/text/BasicTextBackend.cpp) 对 codepoint 或 cluster 遍历 chain，实际调用 `FontRasterizer::hasGlyph()`/cluster 逻辑确认可用 face，并按 weight/slant 选择最佳 face。

这说明 WhatsCanvas 并不是“只按 Unicode range 选字体”；它已经有真实 glyph 检查。差距在于：字体候选集和 fallback 顺序主要由预先构造的 alias 决定，而不是由一个可以按字符询问的平台字体 provider 决定。

### 3.4 DirectWrite 路径是另一种 fallback 机制

Windows DirectWrite backend 与 portable backend 不应混为一谈：

- 构造时取得 DirectWrite system font collection；`IDWriteTextLayout` 可以自动使用系统 fallback resolver。
- 自定义文件/内存字体通过 custom font collection loader 注入。
- 自定义 `FontFallbackChain` 通过 `IDWriteFontFallbackBuilder` 映射全 Unicode range，再追加系统 fallback。
- `refreshSystemFonts()` 同时刷新 `FontSystem` snapshot、DirectWrite system collection 和渲染缓存。

关键证据：[`DirectWriteTextBackend.cpp:340-365`](../../../src/text/DirectWriteTextBackend.cpp)、[`DirectWriteTextBackend.cpp:400-507`](../../../src/text/DirectWriteTextBackend.cpp)、[`DirectWriteTextBackend.cpp:990-1060`](../../../src/text/DirectWriteTextBackend.cpp)。

当前需要留意一个契约风险：`DirectWriteBackendOptions` 声明了 `enableSystemFontFallback`，但 DirectWrite 构造和 layout 路径仍直接获取并使用 system font collection；当前源码没有证明该开关能真正关闭系统 fallback。文档和测试不应先承诺这一语义。

### 3.5 Android 已迁移为 core 按字符 Provider

Android 宿主原先用 `A` 和 `中` 两个代表字符探测字体，再注册
`AndroidSans`/`AndroidCjk` alias。该逻辑现已从示例 JNI 删除，迁移到
[`AndroidFontProvider.cpp`](../../../src/text/platform/AndroidFontProvider.cpp)：

- API 29+ 对每个真实 cluster 调用 `AFontMatcher`，传入 generic family、
  weight、slant 和 BCP-47 locale；只有 matcher 覆盖完整 UTF-16 cluster 时
  才接受结果，之后仍由 rasterizer 做真实 glyph coverage 复核。
- 返回的 path 在 `AFont` 有效期内被读取为不可变共享字节，连同 collection
  index、实际 weight/slant 进入 memory-backed `FontFace`；后续不重开该路径，
  匹配结果按 family/style/locale/cluster 缓存。超过防御性快照上限时才保留
  file-backed 兼容回退。
- API 21-28 因没有公开 NDK matcher，解析 system/vendor/product 的
  `fonts.xml` 及旧 `system_fonts.xml`/`fallback_fonts.xml`，保留 alias、
  locale、weight/slant、TTC index、`fallbackFor` 和配置顺序；少量
  Roboto/Noto 固定路径仅作为配置缺失或损坏后的最后兜底。
- `refreshSystemFonts()` 会刷新 platform provider generation，并清理
  resolver、loaded face、shape/layout 与 atlas 缓存。

因此 Android 已不再用两个字符推断整个设备覆盖。VS15/VS16 会保留在
provider 请求中以排序 text/emoji presentation family，但不会被当成独立
可见 glyph。启用 FreeType 时，portable rasterizer 已能解释 Android 当前
`NotoColorEmoji.ttf` 使用的常见 COLRv1 paint graph 并上传 RGBA atlas；OEM
私有/无路径字体、其他 color font 容器和高级 composite 的完全一致性仍是
后续能力边界。

### 3.6 与发现直接相关的缓存和能力边界

系统字体 refresh 的进程内失效链已经闭合：
`BasicTextBackend::refreshSystemFonts()` 会刷新 discovery/provider generation，
清理 shape/layout/face 选择缓存、`FontRasterizer` loaded-face LRU、glyph atlas，
以及 Windows native measure/bitmap cache；然后重新注册系统 fallback 并恢复用户
fallback chain。仍待补齐的是 host 层字体变化通知和安全 relayout，以及跨进程或
外部文件替换的 fingerprint/监听策略。

另外，当前 `FontFace::isValid()` 只检查 family/path/bytes 非空，不检查文件存在性、字体格式或 face index（[`Font.h:121-126`](../../../include/wsc/Font.h)）。这意味着“注册成功”与“可解析、可渲染”之间存在延迟失败。

与 Flutter 的字体管理器相比，WhatsCanvas 还缺少几个会反过来影响匹配结果的能力：

- Android 系统配置和 NDK matcher 返回的 variation axis 已进入 `FontFace`；
  `Paint::setFontVariation()` 可覆盖系统实例轴，并贯通 HarfBuzz、FreeType、
  DirectWrite 与 glyph/face cache identity；
- portable rasterizer 已支持 COLR/CPAL v0、常见 COLRv1 paint graph，以及
  常用的 CBLC index 1 / CBDT image 17 PNG；其他 bitmap index/image format、
  sbix、SVG 或任意高级 COLRv1 composite 仍不能仅因被发现就视为可渲染；
- family 匹配以精确 key 和简单 weight/slant 距离为主，尚缺 stretch、variable axis、真实 style metadata 等信息；
- 发现、backend、atlas 的线程模型依赖外部约束，建议在接口中明确 Canvas/backend thread-confined，避免把 discovery 的 mutex 误认为整个字体系统可并发。

这些不是“是否能枚举到字体”的问题，但会决定发现结果最终能否被正确选择和绘制，应该纳入后续 resolver 设计。

## 4. Flutter 当前实现

### 4.1 FontCollection 的管理器组合

Flutter engine 的核心抽象位于 `G:\flutter-master\engine\src\flutter\txt\src\txt\font_collection.cc` 与 `G:\flutter-master\engine\src\flutter\txt\src\txt\font_collection.h`。当前 checkout 中 `GetFontManagerOrder()` 的顺序是：

1. dynamic font manager
2. asset font manager
3. test font manager
4. default/platform font manager

集合设置变化时会 reset SkParagraph 的 font collection；`ClearFontFamilyCache()` 用于动态字体注册后清除 family cache。默认情况下 fallback 开启，测试 manager 可以显式关闭 fallback。

关键证据：`G:\flutter-master\engine\src\flutter\txt\src\txt\font_collection.cc:34-110`。

与 WhatsCanvas 的差别是：Flutter 把不同来源当成并列的 `SkFontMgr`/provider，而不是先把所有来源合并成一张 `FontFace` 列表。这个分层让“应用字体覆盖系统字体”“测试字体隔离”“动态字体增量注入”成为管理器优先级问题。

### 4.2 平台默认字体管理器

在当前 Flutter checkout：

- Windows：`SkFontMgr_New_DirectWrite()`，默认 family 候选为 `Segoe UI`、`Arial`。
- Linux：优先 `SkFontMgr_New_FontConfig()` + FreeType scanner；若不可用，再按构建能力使用目录、空 manager 等后备。
- Android：优先 `SkFontMgr_New_Android()` + FreeType scanner，默认 family 为 `sans-serif`。

关键证据：

- `G:\flutter-master\engine\src\flutter\txt\src\txt\platform_windows.cc:10-15`
- `G:\flutter-master\engine\src\flutter\txt\src\txt\platform_linux.cc:22-39`
- `G:\flutter-master\engine\src\flutter\txt\src\txt\platform_android.cc:18-31`

Flutter 的默认路径是“把平台字体系统交给 `SkFontMgr`”，而不是应用层枚举成 `family/path/index` 快照。它因此更适合按需匹配，也减少了因为字体没有稳定路径而无法参与渲染的问题。

### 4.3 应用资产字体和动态字体

Flutter UI 层 `G:\flutter-master\engine\src\flutter\lib\ui\text\font_collection.cc` 有两类重要输入：

- `RegisterFonts()` 读取 `FontManifest.json`，构造 `AssetManagerFontProvider`，按 family 注册 asset 路径；`AssetManagerFontStyleSet::createTypeface()` 在真正匹配时才从 asset manager 读取并创建 `SkTypeface`。
- `LoadFontFromList()` 接收内存字体数据，创建 `SkTypeface`，注册到 dynamic manager，并调用 `ClearFontFamilyCache()`。

Dart 层 `G:\flutter-master\packages\flutter\lib\src\services\font_loader.dart:11-76` 用 `FontLoader` 聚合一个 family 的字体数据，直到 `load()` 才逐个调用 engine 的 `loadFontFromList`。

这给 WhatsCanvas 的直接启发是：注册接口可以接受文件/内存，但加载、解析和 style set 构造不必在注册时全部完成；应支持“登记来源 → 首次使用懒加载 → 命中后缓存”。

需要记录一个当前源码事实：`flutter/lib/ui/text/font_collection.cc:121-123` 的注释仍写着 `TODO(chinmaygarde): Handle weights and styles.`，当前 manifest 注册路径只显式传 family 和 asset，不能把该实现描述成完整的 manifest weight/style 元数据映射。

### 4.4 Flutter 的 fallback 语义

Flutter 的 `txt::FontCollection::CreateSktFontCollection()` 将 default/asset/dynamic/test manager 交给 SkParagraph。family/style/character 的具体匹配由 `SkFontMgr`、SkTypeface style set 和 SkParagraph 完成；平台 manager 负责系统字体与字符匹配，provider 负责应用字体集合。

与 WhatsCanvas portable 路径相比，Flutter 的关键点不是“有一条固定的 CJK/Arabic/Symbol family 字符串链”，而是有一个可以回答“这个 family/style 在这个字符和 locale 下应该使用哪个 typeface”的统一管理器接口。对复杂脚本和 locale 相关 fallback，这个接口比手工 Unicode slot 更接近实际排版需求。

### 4.5 Flutter 的系统字体 reload 语义

当前 checkout 还提供了 engine 级 `Shell::ReloadSystemFonts()`：重新设置 default font manager，清理 SkParagraph family cache，并向 framework 发送 `fontsChange` 通知（`G:\flutter-master\engine\src\flutter\shell\common\shell.cc:2427-2439`）。它与 WhatsCanvas 的 `FontSystem::refreshInstalledFonts()` 都是显式刷新，但粒度不同：WhatsCanvas 暴露 discovery generation 和 default-slot cache；Flutter 重新建立 platform manager 连接并通知上层，而不是把所有系统 face materialize 成快照。

Flutter 的资产 cache 也不是全量清理：`ClearFontFamilyCache()` 只清理 textlayout family cache，资产条目中已经创建的 `SkTypeface` 仍由 provider 持有（`asset_manager_font_provider.cc:101-131`）。这点值得 WhatsCanvas 借鉴：缓存失效应区分“family resolution cache”“loaded typeface/face cache”和“最终 render/atlas cache”，不要用一个刷新动作掩盖所有生命周期。

### 4.6 Android Skia 行为 Oracle

当前 Flutter checkout 的 `DEPS` 将 Skia 固定在
`653397c6be15b87fe8f89a4492582fbb825f6da8`。该 revision 已按 shallow、
partial clone 同步到 Flutter engine 预期目录，并完成最小 CPU/Expat 构建。
实现不复制一份 Skia，而是把 Flutter 选定的 Android `SkFontMgr` 行为作为
兼容性 Oracle，并用独立 fixture 锁定以下语义：

- `<font fallbackFor="...">` 会从父命名 family 中拆出，成为只服务于指定
  family 的 fallback；不能同时作为父 family 的普通 face。
- 带 `weight` 的 `<alias>` 不是“把请求 weight 改掉后近似匹配”，而是只由
  target family 中精确等于该 weight 的 face 构成一个新 family。
- family 的 `lang`、`variant="compact|elegant"`，旧 `<file>` 的
  `lang/variant/index`，以及 `<axis tag="...." stylevalue="...">` 都属于字体
  实例语义。需要注意固定 revision 的 Skia 会保留重复 axis，且旧格式会把最后
  一个 `<file>` 的 lang/variant 应用到整个 family；WhatsCanvas 选择逐 face 保留
  旧元数据并拒绝重复 axis，这两项是已声明的兼容/安全增强，不伪装成 exact parity。
- XML 省略 weight/style 时必须扫描字体内部 `OS/2/head/post`；显式 XML
  始终优先。style set 使用 CSS3 weight/slant 顺序，不能只按绝对 weight 距离。
- character fallback 先搜索 `fallbackFor=请求 family`，再搜索全局 fallback；
  默认 variant pass 先 elegant/default，再 compact。
- variable-font 轴必须同时影响 shaping 与 rasterization，并进入缓存 identity；
  只在 XML 解析结果中保存 axis 仍会产生排版/图像不一致。

对应实现和回归测试在
[`AndroidFontConfig.cpp`](../../../src/text/platform/AndroidFontConfig.cpp)、
[`AndroidFontProvider.cpp`](../../../src/text/platform/AndroidFontProvider.cpp) 和
[`AndroidFontConfigTests.cpp`](../../../tests/AndroidFontConfigTests.cpp)。此外，
[`AndroidFontOracleProbe.cpp`](../../../tools/font_oracle/AndroidFontOracleProbe.cpp)
现在用生产解析器输出版本化的 face/alias/query 快照，
[`SkiaAndroidFontOracleProbe.cpp`](../../../tools/font_oracle/SkiaAndroidFontOracleProbe.cpp)
直接调用固定 revision 的 `SkFontMgr_android_parser`，与 WhatsCanvas 生产解析器
保持链接和实现隔离；
[`run_android_font_oracle.py`](../../../tools/font_oracle/run_android_font_oracle.py)
负责 golden 与可选 Skia 双实现的首差异比较；CTest 在执行前强制构建探针，
避免旧二进制造成假通过。协议和固定 revision 接入方法见
[`tools/font_oracle/README.md`](../../../tools/font_oracle/README.md)。参考行为来自
[Skia Android font config parser](https://skia.googlesource.com/skia/+/main/src/ports/SkFontMgr_android_parser.cpp)、
[Skia Android font manager](https://skia.googlesource.com/skia/+/main/src/ports/SkFontMgr_android.cpp)
与 [Android NDK font API](https://developer.android.com/ndk/reference/group/font)。

当前仓库已具备离线 golden gate 和严格双引擎 runner，本机固定 revision 的
Skia/Expat producer 已构建，`WhatsCanvasAndroidFontOracleSkia` 严格差分测试通过。
CTest 只在显式开启 `WHATSCANVAS_ENABLE_EXTERNAL_FONT_ORACLES` 并配置对应 probe
路径时注册外部参考测试；相关变量与注册逻辑隔离在 `tools/font_oracle/cmake`，确保
普通离线构建不会误报双引擎覆盖；producer 的独立 CMake 配置会核验 Skia
以及 Expat、FreeType、libpng、zlib 的固定 revision。
`WhatsCanvasAndroidFontOracleCorpusSkia` 进一步覆盖 API 21–35 与 vendor/product
最小 corpus：API 21 family-list/大小写 alias 与 API 23/29/33 共 5 组做严格
等价，旧格式逐 face 元数据、重复 axis 拒绝、`oblique` 和空 font 过滤则以
“预期首差异路径 + 原因”锁定为显式扩展。
固定 revision 的 FreeType/libpng/zlib 依赖也已同步并加入最小构建；
`WhatsCanvasSkiaFontScannerGolden` 会实际打开 bundled Roboto Flex，锁定 1 个 face、
20 个 named instances、13 条 axis 的范围/default/position 以及 intrinsic style。
因此当前已经明确分开“虚拟路径的 XML 语义”和“真实文件的 font-table discovery”。
`WhatsCanvasSkiaAndroidFontManagerGolden` 再向上验证真实 `SkFontMgr_New_Android`：
配置 family 可枚举，`700 italic` 请求会落到文件真实的 `400 normal`，Latin `A`
得到 glyph 36；U+4C2E 会按 `zh-Hans`、`ja` 分别落到两个 Source Han fallback，
U+2049 会按 `und-Zsye` 落到 Noto Color Emoji fallback；这些 fallback 都不出现
在普通 family 枚举中，仍未覆盖的 U+4E2D 返回无匹配。

## 5. 差距矩阵

| 维度 | WhatsCanvas 当前做法 | Flutter 当前做法 | 差距判断 |
| --- | --- | --- | --- |
| 系统字体入口 | CoreText/DirectWrite/fontconfig 枚举 face，转成本地文件记录 | 平台 `SkFontMgr` 按需查询 | WhatsCanvas 更可审计；Flutter 更少依赖稳定文件路径 |
| 默认字体策略 | 硬编码平台候选 family，生成 `WhatsCanvas *` alias | 平台 manager + 少量 default family hints | WhatsCanvas 策略更显式，但 alias 和平台知识耦合更重 |
| fallback 决策 | `FontResolver` 组合 provider，保留显式 family chain，并对完整 cluster 做 glyph 检查 | manager/SkParagraph 按 family、style、character、locale 匹配 | Android 已按需查询；其他平台 provider 仍需补齐 |
| glyph 覆盖 | 有 `FontRasterizer` 实查，但候选集由 alias/range 限制 | 交给 `SkFontMgr`/SkTypeface/SkParagraph | Flutter 更接近按字符动态解析；两者都不能仅凭 family 宣称完整覆盖 |
| 复杂脚本/locale | 基础 cluster fallback；DirectWrite 可利用 locale | 平台 manager 与 SkParagraph 组合 | portable WhatsCanvas 需要增强 locale 与 cluster 语义 |
| 自定义字体 | `FontFace` 文件/内存 + 调用者提供 descriptor | asset manifest provider + dynamic memory provider | Flutter provider 化、懒加载和来源隔离更成熟 |
| style 匹配 | weight/slant 归一化，portable 用距离评分 | `SkFontStyle`/style set；平台 manager 保留更多原生信息 | WhatsCanvas 需补 stretch、variable axis、oblique 等信息 |
| TTC/OTC | 显式 `faceIndex`，发现阶段也保留 index | `SkTypeface`/font manager 内部处理 ttc index | WhatsCanvas API 清晰，但需更多跨平台测试 |
| 缓存 | discovery snapshot、default slot cache、generation、render cache | font collection cache；动态注册后清 family cache；typeface 可懒加载 | WhatsCanvas refresh 更显式；Flutter cache 边界更贴近 provider |
| 系统字体刷新 | 有统一 `refreshInstalledFonts()`，DirectWrite 也刷新 collection | engine 有 `ReloadSystemFonts()`，清 family cache 并发送 `fontsChange`；framework 下一帧 relayout | WhatsCanvas 已有 generation，但仍需补 host 通知与安全 relayout 协议 |
| 测试隔离 | 有 discovery/FontManager/DirectWrite tests | 有独立 test font manager，能关闭 fallback | Flutter 的 test manager 设计值得借鉴 |
| Android | API 29+ 按 cluster/locale/style 调 `AFontMatcher` 并在句柄关闭前快照字节；API 21-28 解析平台 font config | Android 默认 `SkFontMgr_New_Android` | matcher 结果已解除后续稳定路径要求；旧 API 仍依赖系统分区文件生命周期 |
| 可变字体 | 系统实例轴与 `Paint` 覆盖已贯通 HarfBuzz、FreeType、DirectWrite 和 cache identity | Flutter 文本路径可携带 `FontVariations`/`SkFontArguments` | 渲染主链已打通；stretch、轴范围等 discovery metadata 仍待完成 |
| color font | 支持 COLR/CPAL v0、常见 COLRv1 paint graph，以及 CBDT/CBLC index 1 + image 17 PNG | Flutter/Skia 测试覆盖真实 COLR 与 CBDT color glyph | COLR/CBDT 已建立跨引擎差分门禁；其他 bitmap index/image format、sbix、SVG 仍待补齐 |

## 6. 建议借鉴方案

### P0：建立 provider/resolver 分层

保留 `FontSystem::discoverInstalledFontFaces()` 作为字体选择器、诊断和预热工具，但渲染主链不要要求先得到完整 face 列表。建议抽象如下语义：

```text
FontProvider
  enumerateFamilies()                      // 可选，供 UI/诊断使用
  matchFamilyStyle(family, style)           // 返回 style set 或候选 faces
  matchFamilyStyleCharacter(family, style,
                            locale, codepoint/cluster)
  loadTypeface(source, faceIndex)           // 可懒加载并缓存

FontCollection
  dynamic provider
  asset provider
  test provider
  platform provider
```

portable backend 通过这个接口工作；DirectWrite provider 可以把调用转给 `IDWriteFontCollection`/`IDWriteFontFallback`；Android provider 可以把调用转给 `AFontMatcher`；CoreText/fontconfig provider 则按平台能力实现。

### P0：把“按字符匹配”提升为一等 API

当前 `FontFallbackChain` 只保存 family 顺序。建议新增至少以下输入：

- family 候选或 primary family
- weight、slant、stretch
- BCP-47 locale
- 单个 codepoint 或不可拆分 cluster
- 可选 variation selector / emoji presentation 信息

返回值应包括：实际 provider、实际 family、实际 face identity、是否 fallback、是否覆盖完整 cluster。这样可以保留现有 cluster 级策略，同时把“候选集如何获得”交给 provider。

### P1：保留 discovery snapshot，但丰富元数据

`DiscoveredFontFace` 当前只有 family/path/faceIndex/weight/italic。建议增加：

-真实 family name、PostScript name、full name；
- style、stretch、variable axes；
- source kind（system file、memory、asset、platform-internal/cloud）；
- provider name 和平台原始 identity；
- 可选字体文件 fingerprint；
- 可选、明确标注为“探测结果”的 Unicode coverage 摘要。

不要把 alias 覆盖范围当成真实字体 coverage；如果需要 coverage，应从 cmap 或平台 API 生成并单独缓存。

### P1：应用字体 provider 化和懒加载

借鉴 Flutter `AssetManagerFontProvider`/dynamic manager：

1. 注册时只保存 source descriptor、family alias、style metadata、face index。
2. 首次匹配或 rasterize 时才读取文件/内存并创建 `LoadedFace`。
3. 按 `(provider, family, style, source identity, faceIndex)` 缓存 typeface/字体解析结果。
4. 注册新字体或替换 provider 时只清理受影响的 family cache 和 render cache。

这比当前“注册后直接进入统一列表，再由 rasterizer 负担全部加载”更容易控制启动成本和错误边界。

### P1：补齐 locale 和 style 语义

portable 与 native backend 的公共 `Paint`/resolver 输入应一致：family、weight、slant、stretch、locale、OpenType features、variation axes。尤其是 Han unification、阿拉伯文字 shaping、emoji presentation 不能只靠固定 Unicode range 解决。

### P1：增加测试字体管理器

借鉴 Flutter 的 test manager：允许测试注入一个可控 provider，并可关闭系统 fallback。这样可以稳定测试：

- family/style 选择；
- 缺字时的 provider 顺序；
- cluster 不能被拆开的规则；
- dynamic/asset/system 优先级；
- refresh 后哪些缓存必须失效；
- 无本地路径字体的失败语义。

### P2：对 discovery 和 fallback 分别提供诊断

建议日志和诊断对象至少区分：

- `discovery_snapshot`: 平台枚举到什么；
- `registration`: 哪些 source 成功进入 provider；
- `resolution`: 某个字符/cluster 最终用了哪个 face；
- `fallback_reason`: 首选 family 缺 glyph、style 不可用、locale 选择、provider 不可用等；
- `cache`: snapshot generation、family cache hit/miss、render cache invalidation。

## 7. 不建议直接照搬的部分

- 不建议为了获得 Flutter 的架构而整体引入 Skia `SkFontMgr`；WhatsCanvas 已经有 FreeType/STB/HarfBuzz、DirectWrite 和自己的 raster/cache 体系，直接替换会扩大二进制、许可证、构建和调试成本。
- 不建议用少量代表字符推断完整字体覆盖。Android 已改为真实 cluster
  查询，其他平台与诊断工具也应保持这个约束。
- 不建议把所有系统字体预加载进内存。Flutter 的 asset 路径本身体现了按需创建 typeface 的价值；系统字体也应尽量由平台 manager 延迟解析。
- 不建议把平台自动 fallback 和应用显式 fallback chain 混成一个无优先级列表。DirectWrite 的 system resolver、portable 的 explicit chain、Android 宿主的 alias chain 应能在诊断中区分。
- 不建议把“发现成功”作为“渲染成功”或“多语言覆盖完成”的测试结论。必须加入实际 glyph/cluster/渲染断言。
- 不建议把 `FontRasterizer` 的线程安全误读为整个 Canvas/backend 可并发；refresh、atlas、DirectWrite 和 render cache 仍应按明确的线程归属使用。

## 8. 建议落地顺序

### 阶段 1：契约和测试先行

- 定义 provider/resolver 接口和 resolution result。
- 增加 test provider，覆盖 family/style/character/cluster/locale。
- 明确系统字体为空、字体无本地路径、TTC index 无效、provider 失败时的返回值和日志。
- 修正文档与实现不一致的 `enableSystemFontFallback` 语义。

截至 2026-08-16，阶段 1 的第一批核心能力已经实现：

- 新增公共 [`FontResolver.h`](../../../include/wsc/FontResolver.h)，定义 `FontProvider`、provider 层级、`FontMatchRequest`、`FontMatchResult` 和 coverage predicate。
- `FontManager` 通过 `FontManagerProvider` 接入 resolver，保留旧注册、查找和 fallback API。
- family lookup 已统一 ASCII 大小写、首尾空白和重复空白，同时保留原始 display family。
- portable style 匹配改为接近 CSS Fonts 的 weight 搜索顺序，并继续优先匹配 slant。
- dynamic 与 system manager 已在 portable backend 中分层；系统刷新不再清空并重放用户字体。
- locale、完整 cluster、provider 名称、是否 fallback、coverage 是否验证已进入匹配契约。
- resolution/shape/layout cache key 已包含 resolver generation 和 locale。
- 系统刷新现在会清 `FontRasterizer` loaded-face LRU，修复同路径字体替换后可能复用旧数据的问题。
- `FontManager` lookup 使用稳定 face 存储，避免注册新 face 时 `vector` 扩容导致缓存指针悬空。
- 新增 provider 优先级、canonical family、CSS weight、cluster coverage、locale 透传和 generation 测试。

### 阶段 2：portable backend 接入 provider（第一阶段已完成）

- 已将现有 `FontManager` 包装为 dynamic/system provider。
- 已保留 `FontSystem` alias 作为兼容默认 family，并让 Android platform
  provider 参与同一 family 的按需匹配。
- 已由 rasterizer 对完整 cluster 做实际 glyph coverage 复核。
- 已把 family/style/character/locale/resolver generation 纳入 raster face、
  shape 和 layout cache key。

### 阶段 3：资产和动态字体懒加载（公共基础链路已完成）

- 已新增公共 `LazyFontProvider`/`LazyFontSource`：注册阶段只保存 source id、
  family/style、TTC index、coverage ranges 和 variation axes；首次 family match
  才通过回调取得字节并创建 memory `FontFace`。
- loader 在 provider 锁外执行；同 source 的成功/失败结果都会缓存，
  `invalidateFamily()` 可安全重试，注册相同 `(family, sourceId)` 会原位替换。
- `Canvas::addFontProvider()` 已把 provider 接入 portable 与 DirectWrite backend；
  `FontResolver::resolutionGeneration(family)` 只组合该 family 及显式 fallback
  chain 的版本，避免不相关 family 失效 shape/layout/face cache。
- DirectWrite 已在首次使用 family 时把优先级最高 provider 的 file/memory face
  注入 native custom collection；family generation 变化时替换旧 source 并避开
  旧 bitmap cache。
- 已新增 `RemoteFontProvider`：按缺失 cluster 的声明 coverage 贪心选择 subset，
  `match()` 只排队而不阻塞；宿主通过 `takeDownloadRequests()` 领取请求，并用
  `completeDownload()`/`failDownload()` 回填。状态机包含去重、并发/重试上限、
  永久失败记忆、累计字节预算、过期回调 token、family 定向 generation，以及
  `takeChangedFamilies()` 合并通知。`LazyFontSource::fingerprint` 允许配置重放
  保留已加载/下载状态，内容版本变化则精确失效并隔离旧回调。待完成浏览器
  fetch/`FontFaceSet` adapter 和宿主帧调度。

### 阶段 4：平台能力增强

- Windows：完善 DirectWrite fallback/locale/variable-font 能力，并确认 custom chain 与 system resolver 的优先级。
- Android（API 29+ 无稳定路径要求已完成）：`AFontMatcher` 已从 sample
  迁入正式 provider；按 cluster 匹配后在 `AFont` 关闭前快照字节。API 21-28
  解析系统 font config，并以系统分区路径作最后兜底。
- Android color emoji（基础链路已完成）：VS16 优先 emoji family，VS15
  降低 emoji family 排名；默认 emoji、ZWJ、肤色、旗帜、keycap 与 tag
  sequence 也会按完整 cluster 选择 emoji family。HarfBuzz 生成 GSUB 合字，
  FreeType/portable color path 合成 RGBA glyph atlas，并已在 API 33 模拟器验证。
- macOS：区分 CoreText discovery 与 CoreText text backend 是否已实现，避免文档把两者混为一谈。
- Linux：分别测试 fontconfig 可用、未链接和空 manager 构建。

截至 2026-08-17，Android 系统字体链路已完成：
`AFontMatcher` 已进入 core system provider，示例 JNI 不再负责系统字体
发现；API 29+ 支持真实 cluster/locale/weight/slant 查询、collection
index 和 generation cache；API 21-28 已支持新旧 font config schema、
vendor/product 合并、alias weight 与简繁 locale 排序；API 33 已验证 VS16
选择、COLRv1 栅格和 RGBA atlas 输出。随后又补齐了 `fallbackFor` 隔离、
weighted alias 精确子族、命名 product family、legacy `lang/variant`、XML/NDK
variation axis 到 HarfBuzz/FreeType/cache 的完整传递，以及缺省 style 的
`OS/2/head/post` 扫描、CSS3 style 顺序、专属 fallback 和默认 variant pass。
Android API 29+ matcher 结果现在会在 `AFont` 关闭前物化为不可变共享字节，
shaping/raster 不再依赖后续仍可打开的稳定文件路径；API 21-28 的配置路径仍由
系统分区生命周期保证，并在 refresh 后重新解析。真实配置 corpus 已加入 Google
API 33 AVD（367 faces / 24 aliases）和 Redmi K30 / MIUI 12.5 Android 11
（360 faces / 24 aliases）的完整 `/system/etc/fonts.xml`，且与固定 Skia parser
执行分类差分。仍待增加更多 Android 大版本、更多 OEM/product/vendor 分区样本
以及真实 color font golden；ZWJ/肤色、区域旗帜和 keycap 已有分类、UTF-16
转发、构建合同与 API 33 运行验证。

## 9. 验证矩阵

| 场景 | 期望验证 |
| --- | --- |
| Latin + CJK 同一 cluster/字符串 | fallback 是否在正确边界切换，是否保持 shaping 正确 |
| Arabic / Hebrew | locale、bidi、style 和 glyph coverage 是否同时生效 |
| emoji、非 BMP、variation selector | 是否选择 color font/正确 face，是否错误拆 cluster |
| TTC/OTC 多 face | `path + faceIndex` 是否稳定，错误 index 是否可诊断 |
| variable font | axis 是否保留或明确降级，weight 是否被错误离散化 |
| 云字体/无本地路径 | discovery 是否跳过，platform provider 是否仍可渲染 |
| 安装/删除系统字体 | snapshot generation、default slot、provider 和 render cache 是否按契约刷新 |
| 动态内存字体 | 注册前不可见、注册后可见、family cache 被清理、旧 render cache 不复用 |
| 同名 family 多来源 | dynamic/asset/test/system 的优先级是否稳定 |
| 无 fontconfig / 空平台 manager | 构建可运行，错误和 fallback 行为可观察 |

当前 WhatsCanvas 的 `SystemFontDiscoveryTests` 主要验证发现记录自洽、注册成功、fallback 顺序和 generation refresh；空结果会按平台能力跳过，因此不能把现有测试描述成完整多语言 coverage 证明。建议把“发现记录测试”和“实际 glyph/cluster 渲染测试”拆开。

## 10. 参考源码

### WhatsCanvas

- [`include/wsc/Font.h`](../../../include/wsc/Font.h)
- [`src/text/SystemFontEnumerator.h`](../../../src/text/SystemFontEnumerator.h)
- [`src/text/SystemFontEnumerator.cpp`](../../../src/text/SystemFontEnumerator.cpp)
- [`src/text/BasicTextBackend.cpp`](../../../src/text/BasicTextBackend.cpp)
- [`src/text/DirectWriteTextBackend.cpp`](../../../src/text/DirectWriteTextBackend.cpp)
- [`src/text/FontRasterizer.cpp`](../../../src/text/FontRasterizer.cpp)
- [`src/text/FontRasterizer.h`](../../../src/text/FontRasterizer.h)
- [`src/text/platform/AndroidFontConfig.cpp`](../../../src/text/platform/AndroidFontConfig.cpp)
- [`src/text/platform/AndroidFontProvider.cpp`](../../../src/text/platform/AndroidFontProvider.cpp)
- [`platforms/android/app/src/main/cpp/whatscanvas_jni.cpp`](../../../platforms/android/app/src/main/cpp/whatscanvas_jni.cpp)
- [`tests/SystemFontDiscoveryTests.cpp`](../../../tests/SystemFontDiscoveryTests.cpp)
- [`doc/DIRECTWRITE_TEXT_BACKEND.md`](../../public/guides/text/DIRECTWRITE_TEXT_BACKEND.md)

### Flutter

以下链接指向本机 `G:\flutter-master` 源码；如果阅读环境不能访问 G 盘，请使用文首路径和行号定位：

- `G:\flutter-master\engine\src\flutter\txt\src\txt\font_collection.cc`
- `G:\flutter-master\engine\src\flutter\txt\src\txt\font_collection.h`
- `G:\flutter-master\engine\src\flutter\txt\src\txt\platform_windows.cc`
- `G:\flutter-master\engine\src\flutter\txt\src\txt\platform_linux.cc`
- `G:\flutter-master\engine\src\flutter\txt\src\txt\platform_android.cc`
- `G:\flutter-master\engine\src\flutter\txt\src\txt\asset_font_manager.cc`
- `G:\flutter-master\engine\src\flutter\txt\src\txt\typeface_font_asset_provider.cc`
- `G:\flutter-master\engine\src\flutter\lib\ui\text\font_collection.cc`
- `G:\flutter-master\engine\src\flutter\lib\ui\text\asset_manager_font_provider.cc`
- `G:\flutter-master\engine\src\flutter\shell\common\shell.cc`
- `G:\flutter-master\engine\src\flutter\txt\src\skia\paragraph_builder_skia.cc`
- `G:\flutter-master\packages\flutter\lib\src\services\font_loader.dart`

## 11. 最终判断

WhatsCanvas 不需要放弃现有的跨平台 discovery snapshot、显式 `FontFace` 和 portable rasterizer。更合理的演进方向是：把 snapshot 降级为一种 provider，把平台字体按字符匹配提升为公共 resolver 能力，并把动态/资产/测试字体纳入同一套有优先级的 `FontCollection`。

这样既保留 WhatsCanvas 对文件路径、face index、缓存和渲染可控性的优势，又能吸收 Flutter 在来源分层、按需匹配、懒加载、测试隔离和缓存失效方面的设计经验。

## 12. 综合改造计划（执行版）

### 12.1 目标架构

最终字体链路分为六个职责，不再由一个 `FontManager` 同时承担：

```text
Font discovery（可选枚举，供字体选择器/诊断）
        |
        v
Font providers（dynamic -> asset -> test -> system/platform）
        |
        v
Font resolver（family/style/cluster/locale/fallback）
        |
        v
Typeface/source loader（file/memory/platform handle/stream，懒加载）
        |
        v
Shaping + rasterization（features/variations/color glyph）
        |
        v
Generation + invalidation（resolution/shape/layout/atlas/host relayout）
```

provider 优先级只解决“同一 family 在多个来源中选哪个”；显式 family fallback 仍按 family 顺序执行。这样不会因为 dynamic provider 中存在一个无关 fallback family，就覆盖用户明确请求的 system primary family。

### 12.2 M1：核心 resolver 与兼容迁移（已开始）

范围：

- 完成 provider/resolver 公共契约和现有 `FontManager` 适配。
- portable backend 使用 resolver，不再直接遍历统一 manager。
- dynamic/system 来源分层，保留 Canvas 现有注册 API。
- canonical family、CSS style matching、locale/cluster request、resolution result。
- resolver generation 纳入所有字体选择和排版 cache key。
- 完整修复 refresh 的 loaded-face、shape、layout、face、atlas 缓存边界。

验收：

- 旧 `FontManager`、Canvas 注册与 fallback 测试全部通过。
- 同名 family 时 dynamic 稳定覆盖 system。
- primary 不覆盖完整 cluster 时只能整体落到一个 fallback face。
- provider mutation 后旧 resolution pointer 不悬空，generation 必须变化。
- 同路径字体原地替换并 refresh 后不能复用旧 loaded-face。

### 12.3 M2：平台按字符匹配

状态：Android API 21+ provider、API 29+ matcher 字节快照（不保留稳定路径
要求）和 API 33 常用 COLRv1 emoji 路径已完成；其余平台增强和完整 color
font 格式覆盖仍在进行。

范围：

- Android（已完成）：`AFontMatcher` 已从示例 JNI 移到 core platform
  provider；API 29+ 按真实 cluster、locale、weight、italic 请求，缓存以
  resolver generation 分代。
- Android API 21-28（已完成）：解析 system/vendor/product font config，
  固定文件路径只作最后兜底。
- Windows：把已有 DirectWrite system fallback 包装到相同诊断语义，校验 `enableSystemFontFallback` 的真实开关行为。
- Apple：CoreText provider 使用 descriptor/CTFont 匹配，不能要求稳定文件路径；补系统 Text/Display optical style 和 weight alias。
- Linux：fontconfig provider 支持 `FcFontMatch`/字符集/语言请求，保留全量 list 作为可选 discovery。

验收：

- `骨` 在 `ja-JP`、`zh-CN`、`zh-TW`、`ko-KR` 下有可观测的 resolver 结果。
- Arabic/Hebrew、VS15/VS16、ZWJ emoji、combining mark cluster 不被拆 face。
- Android 不再用 `A`/`中` 两个代表字符推断整个设备覆盖。
- Android matcher 字体在平台句柄有效期内物化为共享内存，关闭句柄后 portable
  shaping/raster 仍可使用；无法物化时必须回退或给出可诊断结果。

### 12.4 M3：资产字体、动态字体与测试隔离

范围：

- 增加 asset provider：注册 source descriptor，首次命中才读取并解析。
- dynamic provider 支持增量替换 family，而不是无限追加同 style face。
- 增加 test provider 和 `disableSystemFallback`，保证 golden/CI 不依赖宿主字体。
- family 定向失效；加载失败区分文件不存在、格式错误、face index 错误和不支持的字体表。
- `FontSourceType` 演进到 file、memory、platform handle、stream provider；明确句柄线程和生命周期。

验收：

- 未使用的 CJK 大字体不进入内存。
- 动态注册后只清受影响 family 的 resolution/shape/render cache。
- 测试 provider 可重复产生跨平台一致结果。
- 云字体、私有系统字体、APK/IPA asset、Web 下载字体不需要伪造文件路径。

### 12.5 M4：可变字体与彩色字体（系统实例基础链路已完成）

范围：

- 已完成：`FontFace` 可保存 OpenType variation coordinate；Android XML
  `<axis>` 与 API 29+ `AFont_getAxis*` 坐标进入该实例；HarfBuzz shaping、
  FreeType design coordinates 和 face/glyph cache identity 使用同一组坐标。
- 已完成：`Paint::setFontVariation()` 支持 `wght/wdth/opsz/slnt/GRAD` 等
  四字节轴；Paint 值覆盖系统实例坐标，CSS weight/slant 仍参与 face 初选。
- discovery metadata 增加 stretch、PostScript/full name、variable axes、source/provider identity 和 fingerprint。
- 在已支持的 COLR/CPAL v0、常见 COLRv1 paint graph 与 CBLC index 1 /
  CBDT image 17 PNG 基础上，继续补齐高级 composite、其他 bitmap
  index/image format、sbix、SVG-in-OpenType；不支持时输出明确降级结果。
- emoji provider 优先级与 text/emoji presentation selector 联动。

验收：

- variable font 不再被错误离散成最接近的静态 weight。
- 同一 glyph 不同 axis 的 atlas/cache identity 不冲突。
- Android、iOS、Windows 常用 color emoji 至少有一条正确渲染路径或明确 native fallback。

### 12.6 M5：字体变化通知与 Web 字体供应（调度核心已完成）

范围：

- 新增 `FontSystem` observer/host hook；字体 generation 变化后合并通知，在安全帧边界请求 relayout/repaint。
- Web 不实现桌面式全量系统枚举；使用 baseline bundled font + 按缺失码点下载 Noto subset。
- 已完成：Web/remote provider 维护 idle/queued/downloading/loaded/permanent-failure 状态，支持并发上限、重试、去重、下载预算和确定性 coverage 候选选择。
- 待完成：浏览器 adapter 执行 fetch、将完成结果调回 provider，并把多个 family generation 变化合并为一次安全帧边界通知。
- 已完成：多个字体状态变化在 `takeChangedFamilies()` 前按 canonical family
  去重；宿主可在安全帧边界只调度一次 fonts-changed/relayout。emoji font
  排在普通 fallback 之前仍需由 Web subset manifest/候选优先级固化。

验收：

- 一帧内多次字体注册只触发一次上层 relayout。
- 字体变化发生在 layout/render 阶段时不会重入破坏当前帧。
- 离线、下载失败、部分 subset 成功时行为稳定且可诊断。
- Web 首屏有确定性 baseline，不因 fallback 尚未下载而崩溃或得到零 metrics。

### 12.7 跨阶段质量门槛

每个里程碑都必须同时满足：

- public header package-consumer 构建通过；Android NDK、Windows MSVC、Apple Clang 至少编译验证。
- unit、text contract、system discovery 和实际 raster/golden 测试分开统计，不能用 discovery 成功代替渲染正确。
- resolution trace 至少包含 request family/style/locale/cluster、provider、resolved face、fallback reason 和 generation。
- 所有缓存都有容量、identity、generation 和失效测试；不得用无界 map 隐式替代 provider cache。
- 新平台实现只接 provider/source/notification 接口，不把宿主 JNI/Objective-C/Web glue 反向渗入通用排版代码。

### 12.8 下一批工作及优先级

1. **已完成：字体内部 style 扫描**：Android XML 省略 weight/style 时，
   WhatsCanvas 只读取 SFNT 目录及 `OS/2/head/post` 小片段，支持 TTF/OTF 与
   TTC/OTC face index；显式配置优先，排序使用 CSS3 style preference。
2. **固定 revision Skia oracle 已完成（P0）**：v1 协议、WhatsCanvas 生产探针、
   语义 fixture、golden gate、首差异诊断、固定 Skia/Expat revision 核验和 CTest
   严格双实现入口已经落地；本机 dual-engine 门禁已通过。跨版本最小 corpus
   也已分成 5 个 strict parity 与 4 个已解释 extension 合同，并补齐 Skia 自测
   覆盖的 family-list 与大小写 alias。真实 Roboto Flex 的 Skia/FreeType scanner
   golden、Android manager 的真实 style/character/locale/emoji 选择门禁，以及
   Latin/CJK/COLR/CBDT 的真实 Skia raster 门禁均已落地。WhatsCanvas 同场景
   golden 与分层差异报告也已完成：glyph/advance 严格匹配，bounds 容差 1px，
   ink area 容差 3%，颜色分类必须一致；像素哈希保持为各引擎独立合同。该门禁
   同时发现 bundled FreeType `FT_DISABLE_PNG=ON` 导致的 CBDT 缺口，并以现有
   stb PNG decoder 补齐常用 CBLC index 1 / CBDT image 17；还修正了启用 FreeType
   时仍优先走 STB COLR v0、造成 metrics 缩小的问题。下一步不是再写一套 parser，
   而是补齐其他 bitmap index/image format、sbix、SVG，并扩充真实 AOSP/OEM
   capture 与复杂 cluster/locale 配置。
3. **首批跨版本/厂商 corpus 已完成，真实设备样本继续扩充（P0）**：
   [`android_font_config`](../../../tests/fixtures/android_font_config/) 已加入 API 21、
   23、28、29、33、35 和 vendor/product 的脱敏最小 fixture，覆盖旧 schema、
   空记录、TTC/axis、locale parent chain、presentation 与 alias target 缺失。
   这些文件代表特性年代而非整份设备配置；后续仍需加入可合法分发的 AOSP 与
   OEM 实机脱敏 capture，覆盖分区合并、重复声明和更复杂的坏条目组合。
4. **Public Paint variations 已完成（P1）**：应用可用
   `Paint::setFontVariation()` 覆盖 `wght/wdth/opsz` 等四字节轴；覆盖值优先于
   `FontFace`/Android XML 实例，并同时进入 HarfBuzz、FreeType metrics/raster、
   shape cache 和 glyph-atlas identity。真实 Roboto Flex 合同测试锁定 `wdth`
   的布局差异和 atlas 隔离；Windows DirectWrite 使用现代 font-set、
   `IDWriteFactory6` 与 `IDWriteTextLayout4`，由 Source Serif variable fixture
   锁定 format/layout/render cache 行为。portable software 路径另有
   `WhatsCanvasVariableFontGoldenTests`：以 bundled Roboto Flex 对 `wdth=50/150`
   分别锁定完整 RGBA hash、墨迹 bounds 和最小差异像素数，防止布局变化但
   raster/cache 仍错误复用。DirectWrite 保留真实布局/bitmap 差异合同；其像素
   受 Windows DirectWrite/ClearType 版本影响，不使用跨系统固定 hash。face
   identity 会按 tag 排序并使用 float 原始位值编码，因此轴设置顺序不影响命中，
   相邻但不同的轴值也不会碰撞；Android provider 的 face 去重复用同一 identity，
   不会再把同路径、同 style 的不同轴实例折叠。
5. **Asset/dynamic lazy provider 与远程调度核心已完成（P1）**：公共 source/loader、
   成功与失败缓存、family 定向 generation、Canvas portable 接入和 package
   consumer smoke 已落地；DirectWrite 也已支持按 family 懒加载、collection
   重建和 render-cache generation 隔离。`RemoteFontProvider` 已实现非阻塞请求、
   coverage 候选、并发/重试/预算、永久失败、过期回调隔离、宿主回填和
   family 变更合并 drain；浏览器 glue 与帧调度待完成。
6. **iOS CoreText provider（P1）**：复用同一 resolver/source/generation 契约，
   用 `CTFontCreateForString`/descriptor 匹配表达无稳定路径字体，不复制 Android XML
   逻辑。
7. **Web host adapter（P1）**：采用 bundled baseline + `RemoteFontProvider`
   按码点下载 subset；实现 fetch/取消、WASM 生命周期、FontFaceSet 可选注册和
   帧边界合并 relayout。Web 不承诺枚举用户全部系统字体，避免隐私、浏览器限制
   与结果不确定性。
