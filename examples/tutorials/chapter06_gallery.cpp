// Chapter 06 comprehensive example: image fit, circular crop and tiling.
#include <wsc/FontSystem.h>
#include <wsc/wsc.h>

#include <cmath>
#include <vector>

namespace {

std::vector<unsigned char> makeLandscape(int width, int height)
{
    // 直接生成 RGBA 测试图，避免教程依赖仓库外部素材。
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 4u);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float fy = static_cast<float>(y) / height;
            unsigned char r = static_cast<unsigned char>(90 + 92 * fy);
            unsigned char g = static_cast<unsigned char>(142 + 62 * fy);
            unsigned char b = static_cast<unsigned char>(224 - 28 * fy);
            // 天空中的太阳。
            const float sunDx = x - width * 0.72f;
            const float sunDy = y - height * 0.30f;
            if (sunDx * sunDx + sunDy * sunDy < 28.0f * 28.0f) {
                r = 255; g = 221; b = 132;
            }
            // 两层山脊和底部水面。
            const float ridgeA = height * 0.58f + std::abs(x - width * 0.34f) * 0.38f;
            const float ridgeB = height * 0.66f + std::abs(x - width * 0.72f) * 0.24f;
            if (y > ridgeA) { r = 54; g = 83; b = 108; }
            if (y > ridgeB) { r = 35; g = 63; b = 83; }
            if (y > height * 0.80f) {
                const int shimmer = ((x / 12 + y / 6) % 2) * 12;
                r = static_cast<unsigned char>(35 + shimmer);
                g = static_cast<unsigned char>(92 + shimmer);
                b = static_cast<unsigned char>(118 + shimmer);
            }
            const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4u;
            pixels[index] = r; pixels[index + 1] = g; pixels[index + 2] = b; pixels[index + 3] = 255;
        }
    }
    return pixels;
}

} // namespace

int main()
{
    // 1. 创建画布并注册用于标题、标签的系统字体。
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Software, 960, 600);
    if (!canvas || !canvas->initializeContext()) return 1;
    for (const auto &face : wsc::FontSystem::defaultSystemFontFaces()) {
        canvas->registerFontFace(face);
    }
    canvas->setFontFallbackChain(wsc::FontSystem::defaultFallbackChain());

    // 2. 将内存中的 RGBA 像素上传为 Image。
    wsc::Image photo;
    const auto pixels = makeLandscape(320, 180);
    if (!canvas->loadImageFromRGBA(photo, pixels, 320, 180, true)) return 2;

    // 3. 绘制页面背景和标题。
    canvas->beginFrame();
    wsc::Paint background;
    background.setColor(wsc::Color(244, 247, 252, 255));
    canvas->drawRect(wsc::RectF(0, 0, 960, 600), background);

    wsc::Paint title;
    title.setColor(wsc::Color(30, 39, 58, 255));
    title.setTextSize(32.0f);
    title.setFontWeight(650);
    title.setTextBaseline(wsc::Paint::TextBaseline::MIDDLE);
    canvas->drawText("One image, four drawing modes", 50, 58, title);

    wsc::Paint imagePaint;
    imagePaint.setColor(wsc::Color(255, 255, 255, 255));
    imagePaint.setAntiAlias(true);
    imagePaint.setImageSampling(wsc::Paint::ImageSampling::MIPMAP_LINEAR);

    // 4. 三个白色面板共用同一个布局函数。
    auto panel = [&](float x, const char *label) {
        const wsc::RectF bounds(x, 112, 260, 300);
        canvas->drawBoxShadow(bounds, 22, 0, 18, 0, 8, wsc::Color(35, 49, 83, 26));
        wsc::Paint surface;
        surface.setColor(wsc::Color(255, 255, 255, 255));
        surface.setAntiAlias(true);
        canvas->drawRoundRect(bounds, 22, surface);
        wsc::Paint caption = title;
        caption.setTextSize(17.0f);
        caption.setFontWeight(600);
        caption.setTextAlign(wsc::Paint::TextAlign::CENTER);
        canvas->drawText(label, x + 130, 376, caption);
    };

    panel(50, "CONTAIN");
    panel(350, "COVER");
    panel(650, "CIRCLE");

    // 5. 同一张图片分别使用 CONTAIN、COVER 和圆形裁剪。
    canvas->drawImageFit(photo, wsc::RectF(72, 142, 216, 190), wsc::Canvas::ImageFit::CONTAIN, imagePaint);
    canvas->drawImageFit(photo, wsc::RectF(372, 142, 216, 190), wsc::Canvas::ImageFit::COVER, imagePaint);
    canvas->drawImageCircle(photo, wsc::PointF(780, 236), 94, imagePaint);

    // 6. 底部区域演示指定 tile 尺寸的平铺。
    const wsc::RectF tiledBounds(50, 466, 860, 84);
    canvas->drawBoxShadow(tiledBounds, 18, 0, 14, 0, 6, wsc::Color(35, 49, 83, 24));
    canvas->drawImageTiled(photo, tiledBounds, 150, 84, imagePaint);

    // 7. 输出与教程图片一致的结果。
    canvas->endFrame();
    return canvas->savePixelsPPM("chapter06_gallery.ppm") ? 0 : 3;
}
