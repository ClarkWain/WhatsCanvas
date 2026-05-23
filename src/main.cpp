#include <iostream>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <array>
#include <limits>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "canvas/Canvas.h"
#include "canvas/Image.h"
#include "canvas/Paint.h"
#include <glm.hpp>
#include <gtc/matrix_transform.hpp>

const float PI = 3.14159265359f;
constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;

std::string getEnvironmentValue(const char* name)
{
#ifdef _MSC_VER
    char* value = nullptr;
    size_t valueSize = 0;
    if (_dupenv_s(&value, &valueSize, name) != 0 || value == nullptr) {
        return std::string();
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

bool parseUint64(const std::string& text, std::uint64_t& value)
{
    if (text.empty()) {
        return false;
    }

    std::uint64_t result = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
        const std::uint64_t digit = static_cast<std::uint64_t>(ch - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
            return false;
        }
        result = result * 10 + digit;
    }

    value = result;
    return true;
}

bool parseFloat(const std::string& text, float& value)
{
    if (text.empty()) {
        return false;
    }

    char* end = nullptr;
    const float parsed = std::strtof(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        return false;
    }

    value = parsed;
    return true;
}

// 回调函数：当窗口大小变化时调整视口
void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    Canvas* canvas = static_cast<Canvas*>(glfwGetWindowUserPointer(window));
    if (canvas && width > 0 && height > 0) {
        canvas->setSize(width, height);
    }
}

int main() {
    std::cout << "Starting application..." << std::endl;

    // 初始化 GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return -1;
    }
    std::cout << "GLFW initialized successfully." << std::endl;

    // 设置 GLFW 上下文版本和 OpenGL 配置
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3); // 主版本号
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3); // 次版本号
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); // 使用核心模式
    glfwWindowHint(GLFW_SAMPLES, 4); // 请求 MSAA，若不支持 GLFW 会回退。

    // 在 macOS 上需要启用兼容性视图
    #ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    #endif

    // 创建窗口
    GLFWwindow* window = glfwCreateWindow(kWindowWidth, kWindowHeight, "PrismCanvas Demo", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    std::cout << "GLFW window created successfully." << std::endl;

    // 设置当前上下文
    glfwMakeContextCurrent(window);
    std::cout << "GLFW context set successfully." << std::endl;

    // 初始化 GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD" << std::endl;
        return -1;
    }
    std::cout << "GLAD initialized successfully." << std::endl;

    // 检查 OpenGL 版本
    std::cout << "OpenGL " << glGetString(GL_VERSION) << " loaded." << std::endl;

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0) {
        framebufferWidth = kWindowWidth;
    }
    if (framebufferHeight <= 0) {
        framebufferHeight = kWindowHeight;
    }

    // 设置视口
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glEnable(GL_MULTISAMPLE);

    // 设置清除颜色
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);


        Canvas::initialize();
    
    Canvas canvas;
        canvas.setSize(framebufferWidth, framebufferHeight);
        glfwSetWindowUserPointer(window, &canvas);
        glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    
    Paint paint1;
    paint1.setStrokeWidth(26.0f);
    paint1.setStyle(Paint::Style::STROKE);
    paint1.setStrokeCap(Paint::StrokeCap::ROUND);
    paint1.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint pathPaint;
    pathPaint.setFillColor(Color(40, 140, 240));
    pathPaint.setStrokeColor(Color::WHITE);
    pathPaint.setStrokeWidth(18.0f);
    pathPaint.setStyle(Paint::Style::FILL_AND_STROKE);
    pathPaint.setStrokeCap(Paint::StrokeCap::ROUND);
    pathPaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint concavePaint;
    concavePaint.setFillColor(Color(180, 95, 245, 210));
    concavePaint.setStrokeColor(Color::WHITE);
    concavePaint.setStrokeWidth(5.0f);
    concavePaint.setStyle(Paint::Style::FILL_AND_STROKE);
    concavePaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint evenOddPaint;
    evenOddPaint.setFillColor(Color(40, 235, 190, 220));
    evenOddPaint.setStrokeColor(Color::WHITE);
    evenOddPaint.setStrokeWidth(4.0f);
    evenOddPaint.setStyle(Paint::Style::FILL_AND_STROKE);

    Paint saveLayerPaint;
    saveLayerPaint.setAlpha(0.82f);

    Paint layerCirclePaint;
    layerCirclePaint.setStyle(Paint::Style::FILL);
    layerCirclePaint.setFillColor(Color(255, 95, 70, 210));

    Paint layerRectPaint;
    layerRectPaint.setStyle(Paint::Style::FILL);
    layerRectPaint.setFillColor(Color(80, 220, 255, 190));
    layerRectPaint.setBlendMode(Paint::BlendMode::ADD);

    Paint roundStrokePaint;
    roundStrokePaint.setStrokeColor(Color(255, 190, 70));
    roundStrokePaint.setStrokeWidth(34.0f);
    roundStrokePaint.setStyle(Paint::Style::STROKE);
    roundStrokePaint.setStrokeCap(Paint::StrokeCap::ROUND);
    roundStrokePaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint curvePaint;
    curvePaint.setStrokeColor(Color(220, 120, 255));
    curvePaint.setStrokeWidth(9.0f);
    curvePaint.setStyle(Paint::Style::STROKE);
    curvePaint.setStrokeCap(Paint::StrokeCap::ROUND);
    curvePaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint rectPaint;
    rectPaint.setFillColor(Color(40, 180, 120));
    rectPaint.setStrokeColor(Color::WHITE);
    rectPaint.setStrokeWidth(8.0f);
    rectPaint.setStyle(Paint::Style::FILL_AND_STROKE);

    Paint roundedCornerPathPaint;
    roundedCornerPathPaint.setStyle(Paint::Style::STROKE);
    roundedCornerPathPaint.setStrokeColor(Color(255, 245, 120, 240));
    roundedCornerPathPaint.setStrokeWidth(5.0f);
    roundedCornerPathPaint.setStrokeCap(Paint::StrokeCap::ROUND);
    roundedCornerPathPaint.setStrokeJoin(Paint::StrokeJoin::ROUND);
    roundedCornerPathPaint.setCornerPathEffect(24.0f);

    Paint transformHitPaint;
    transformHitPaint.setStyle(Paint::Style::STROKE);
    transformHitPaint.setStrokeWidth(10.0f);

    Paint gradientRectPaint;
    gradientRectPaint.setStyle(Paint::Style::FILL_AND_STROKE);
    gradientRectPaint.setStrokeColor(Color::WHITE);
    gradientRectPaint.setStrokeWidth(8.0f);
    gradientRectPaint.setLinearGradient(500.0f, 300.0f, 690.0f, 430.0f,
                                        Color(255, 210, 60, 230), Color(55, 185, 255, 230));
    gradientRectPaint.setShadowLayer(18.0f, 12.0f, 12.0f, Color(0, 0, 0, 115));

    Paint roundRectPaint;
    roundRectPaint.setStyle(Paint::Style::STROKE);
    roundRectPaint.setStrokeColor(Color(250, 90, 90));
    roundRectPaint.setStrokeWidth(12.0f);
    roundRectPaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint asymmetricRoundRectPaint;
    asymmetricRoundRectPaint.setStyle(Paint::Style::FILL_AND_STROKE);
    asymmetricRoundRectPaint.setFillColor(Color(120, 130, 255, 180));
    asymmetricRoundRectPaint.setStrokeColor(Color(255, 255, 255, 220));
    asymmetricRoundRectPaint.setStrokeWidth(4.0f);
    asymmetricRoundRectPaint.setAlpha(0.78f);
    asymmetricRoundRectPaint.setBlendMode(Paint::BlendMode::SCREEN);

    Paint circlePaint;
    circlePaint.setFillColor(Color(255, 140, 70));
    circlePaint.setStrokeColor(Color::WHITE);
    circlePaint.setStrokeWidth(10.0f);
    circlePaint.setStyle(Paint::Style::FILL_AND_STROKE);
    circlePaint.setRadialGradient(610.0f, 505.0f, 62.0f,
                                  Color(255, 245, 170, 245), Color(255, 95, 70, 230));

    Paint ovalPaint;
    ovalPaint.setStyle(Paint::Style::STROKE);
    ovalPaint.setStrokeColor(Color(70, 210, 255));
    ovalPaint.setStrokeWidth(10.0f);
    ovalPaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint arcPaint;
    arcPaint.setStyle(Paint::Style::STROKE);
    arcPaint.setStrokeColor(Color(255, 110, 210));
    arcPaint.setStrokeWidth(7.0f);
    arcPaint.setStrokeCap(Paint::StrokeCap::ROUND);
    arcPaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint dashedPaint;
    dashedPaint.setStyle(Paint::Style::STROKE);
    dashedPaint.setStrokeColor(Color(255, 245, 120, 230));
    dashedPaint.setStrokeWidth(6.0f);
    dashedPaint.setStrokeCap(Paint::StrokeCap::ROUND);
    dashedPaint.setDashPathEffect(std::vector<float>{18.0f, 10.0f, 6.0f, 10.0f}, -12.0f);

    Paint closedDashPaint;
    closedDashPaint.setStyle(Paint::Style::STROKE);
    closedDashPaint.setStrokeColor(Color(120, 255, 210, 220));
    closedDashPaint.setStrokeWidth(3.0f);
    closedDashPaint.setStrokeCap(Paint::StrokeCap::ROUND);
    closedDashPaint.setStrokeJoin(Paint::StrokeJoin::ROUND);
    closedDashPaint.setDashPathEffect(std::vector<float>{22.0f, 12.0f}, -9.0f);

    Paint clipBgPaint;
    clipBgPaint.setStyle(Paint::Style::FILL);
    clipBgPaint.setFillColor(Color(30, 90, 190, 255));

    Paint clipRedPaint;
    clipRedPaint.setStyle(Paint::Style::FILL);
    clipRedPaint.setFillColor(Color(250, 80, 80, 150));

    Paint clipGreenPaint;
    clipGreenPaint.setStyle(Paint::Style::FILL);
    clipGreenPaint.setFillColor(Color(80, 220, 140, 150));

    Paint clipBorderPaint;
    clipBorderPaint.setStyle(Paint::Style::STROKE);
    clipBorderPaint.setStrokeColor(Color::WHITE);
    clipBorderPaint.setStrokeWidth(2.0f);

    Paint clipBoundsPaint;
    clipBoundsPaint.setStyle(Paint::Style::STROKE);
    clipBoundsPaint.setStrokeColor(Color(255, 245, 120, 220));
    clipBoundsPaint.setStrokeWidth(1.5f);

    Paint clipQueryPaint;
    clipQueryPaint.setStyle(Paint::Style::STROKE);
    clipQueryPaint.setStrokeColor(Color(80, 255, 170, 240));
    clipQueryPaint.setStrokeWidth(8.0f);

    Paint imagePaint;
    imagePaint.setColor(Color(255, 255, 255, 210));

    Paint tintedImagePaint;
    tintedImagePaint.setColor(Color(120, 220, 255, 190));
    tintedImagePaint.setImageSampling(Paint::ImageSampling::NEAREST);

    Paint filteredImagePaint;
    filteredImagePaint.setColor(Color(255, 255, 255, 220));
    filteredImagePaint.setImageSampling(Paint::ImageSampling::MIPMAP_LINEAR);
    filteredImagePaint.setColorMatrix(std::array<float, 20>{
        0.393f, 0.769f, 0.189f, 0.0f, 0.0f,
        0.349f, 0.686f, 0.168f, 0.0f, 0.0f,
        0.272f, 0.534f, 0.131f, 0.0f, 0.0f,
        0.0f,   0.0f,   0.0f,   1.0f, 0.0f
    });

    Paint tiledImagePaint;
    tiledImagePaint.setColor(Color(255, 255, 255, 180));
    tiledImagePaint.setImageSampling(Paint::ImageSampling::NEAREST);
    tiledImagePaint.setImageTileMode(Paint::ImageTileMode::MIRROR);

    Paint decalImagePaint;
    decalImagePaint.setColor(Color(255, 255, 255, 180));
    decalImagePaint.setImageSampling(Paint::ImageSampling::NEAREST);
    decalImagePaint.setImageTileMode(Paint::ImageTileMode::DECAL);

    Paint textPaint;
    textPaint.setStyle(Paint::Style::FILL);
    textPaint.setColor(Color(255, 245, 180, 220));
    textPaint.setTextSize(12.0f);

    Paint rotatingTextPaint;
    rotatingTextPaint.setStyle(Paint::Style::FILL);
    rotatingTextPaint.setColor(Color(120, 230, 255, 200));
    rotatingTextPaint.setTextSize(14.4f);
    rotatingTextPaint.setLetterSpacing(1.2f);
    rotatingTextPaint.setTextAlign(Paint::TextAlign::CENTER);
    rotatingTextPaint.setTextBaseline(Paint::TextBaseline::MIDDLE);

    Paint pathTextPaint;
    pathTextPaint.setStyle(Paint::Style::FILL);
    pathTextPaint.setColor(Color(255, 255, 255, 210));
    pathTextPaint.setTextSize(10.0f);
    pathTextPaint.setLetterSpacing(1.0f);

    Image demoImage;
    bool imageLoaded = demoImage.load("images/hello.png");
    if (!imageLoaded) {
        imageLoaded = demoImage.load("images/draw_path.png");
    }

    std::vector<PointF> demoPolylinePoints = {
        PointF(80.0f, 260.0f),
        PointF(130.0f, 230.0f),
        PointF(130.0f, 230.0f),
        PointF(200.0f, 280.0f),
        PointF(260.0f, 230.0f)
    };

    std::vector<Point> demoPolygonPoints = {
        Point(320, 470),
        Point(410, 420),
        Point(470, 500),
        Point(380, 560),
        Point(320, 470)
    };

    Path polygonPath;
    polygonPath.moveTo(320.0f, 470.0f);
    polygonPath.lineTo(410.0f, 420.0f);
    polygonPath.lineTo(470.0f, 500.0f);
    polygonPath.lineTo(380.0f, 560.0f);
    polygonPath.close();
    const auto polygonContourBounds = polygonPath.getContourBounds();

    Path demoPath;
    demoPath.moveTo(80.0f, 80.0f);
    demoPath.lineTo(230.0f, 80.0f);
    demoPath.lineTo(200.0f, 170.0f);
    demoPath.lineTo(110.0f, 190.0f);
    demoPath.close();

    Path concavePath;
    concavePath.moveTo(285.0f, 70.0f);
    concavePath.lineTo(455.0f, 70.0f);
    concavePath.lineTo(410.0f, 125.0f);
    concavePath.lineTo(455.0f, 190.0f);
    concavePath.lineTo(285.0f, 190.0f);
    concavePath.lineTo(335.0f, 125.0f);
    concavePath.close();

    Path evenOddPath;
    evenOddPath.setFillType(Path::FillType::EVEN_ODD);
    evenOddPath.addOval(RectF(690.0f, 245.0f, 72.0f, 72.0f));
    evenOddPath.addRoundRect(RectF(710.0f, 265.0f, 32.0f, 32.0f), 6.0f, 12.0f, 6.0f, 12.0f);
    const RectF evenOddBounds = evenOddPath.getBounds();
    const bool evenOddOuterHit = evenOddPath.contains(700.0f, 280.0f);
    const bool evenOddHoleHit = evenOddPath.contains(726.0f, 281.0f);

    Paint evenOddOuterHitPaint;
    evenOddOuterHitPaint.setStyle(Paint::Style::STROKE);
    evenOddOuterHitPaint.setStrokeWidth(10.0f);
    evenOddOuterHitPaint.setStrokeColor(evenOddOuterHit ? Color(80, 255, 140) : Color(255, 80, 80));

    Paint evenOddHoleHitPaint;
    evenOddHoleHitPaint.setStyle(Paint::Style::STROKE);
    evenOddHoleHitPaint.setStrokeWidth(10.0f);
    evenOddHoleHitPaint.setStrokeColor(evenOddHoleHit ? Color(80, 255, 140) : Color(255, 80, 80));

    Path roundStrokePath;
    roundStrokePath.moveTo(90.0f, 360.0f);
    roundStrokePath.lineTo(180.0f, 270.0f);
    roundStrokePath.lineTo(260.0f, 380.0f);
    roundStrokePath.lineTo(350.0f, 290.0f);
    roundStrokePath.lineTo(430.0f, 410.0f);
    const float roundStrokeLength = roundStrokePath.length();
    const RectF roundStrokeBounds = canvas.measureStrokeBounds(roundStrokePath, roundStrokePaint);
    const bool roundStrokeHit = roundStrokePath.strokeContains(180.0f, 270.0f, roundStrokePaint.getStrokeWidth());
    const bool roundStrokeMiss = roundStrokePath.strokeContains(180.0f, 235.0f, roundStrokePaint.getStrokeWidth());

    Paint roundStrokeHitPaint;
    roundStrokeHitPaint.setStyle(Paint::Style::STROKE);
    roundStrokeHitPaint.setStrokeWidth(9.0f);
    roundStrokeHitPaint.setStrokeColor(roundStrokeHit ? Color(80, 255, 140) : Color(255, 80, 80));

    Paint roundStrokeMissPaint;
    roundStrokeMissPaint.setStyle(Paint::Style::STROKE);
    roundStrokeMissPaint.setStrokeWidth(9.0f);
    roundStrokeMissPaint.setStrokeColor(roundStrokeMiss ? Color(255, 80, 80) : Color(80, 255, 140));

    Paint pathMetricPointPaint;
    pathMetricPointPaint.setStyle(Paint::Style::STROKE);
    pathMetricPointPaint.setStrokeColor(Color(120, 230, 255, 245));
    pathMetricPointPaint.setStrokeWidth(10.0f);

    Paint pathMetricTangentPaint;
    pathMetricTangentPaint.setStyle(Paint::Style::STROKE);
    pathMetricTangentPaint.setStrokeColor(Color(120, 230, 255, 180));
    pathMetricTangentPaint.setStrokeWidth(3.0f);

    Paint pathTrimPaint;
    pathTrimPaint.setStyle(Paint::Style::STROKE);
    pathTrimPaint.setStrokeColor(Color(255, 245, 120, 230));
    pathTrimPaint.setStrokeWidth(6.0f);
    pathTrimPaint.setStrokeCap(Paint::StrokeCap::ROUND);
    pathTrimPaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Paint reversePathTrimPaint;
    reversePathTrimPaint.setStyle(Paint::Style::STROKE);
    reversePathTrimPaint.setStrokeColor(Color(255, 120, 220, 220));
    reversePathTrimPaint.setStrokeWidth(4.0f);
    reversePathTrimPaint.setStrokeCap(Paint::StrokeCap::ROUND);
    reversePathTrimPaint.setStrokeJoin(Paint::StrokeJoin::ROUND);

    Path degeneratePath;
    degeneratePath.moveTo(470.0f, 120.0f);
    degeneratePath.lineTo(470.0f, 120.0f);
    degeneratePath.lineTo(560.0f, 120.0f);
    degeneratePath.lineTo(560.0f, 120.0f);
    degeneratePath.lineTo(630.0f, 190.0f);

    Path curvePath;
    curvePath.moveTo(40.0f, 230.0f);
    curvePath.quadTo(150.0f, 120.0f, 260.0f, 230.0f);
    curvePath.cubicTo(330.0f, 310.0f, 390.0f, 130.0f, 470.0f, 235.0f);

    Path transformRectPath;
    transformRectPath.addRect(RectF(-60.0f, -40.0f, 120.0f, 80.0f));

    const Canvas::TextMetrics rotatingTextMetrics = canvas.measureTextMetrics("Rotating Text", rotatingTextPaint);
    const float rotatingTextClipHalfWidth = rotatingTextMetrics.width * 0.5f + 14.0f;
    const float rotatingTextClipHeight = rotatingTextMetrics.height + 24.0f;
    
    // 动画参数
    const float centerX = 400.0f;
    const float centerY = 300.0f;
    const float radius = 100.0f;
    const int numPoints = 5;
    const float rotationSpeed = 1.0f;
    const float colorSpeed = 0.5f;  // 颜色变化速度
    bool pixelReadbackChecked = false;
    bool captureChecked = false;
    const std::string capturePath = getEnvironmentValue("CPPDEMO_CAPTURE_PPM");
    const bool printPixelHash = !getEnvironmentValue("CPPDEMO_PRINT_PIXEL_HASH").empty();
    const bool exitAfterFirstFrame = !getEnvironmentValue("CPPDEMO_EXIT_AFTER_FIRST_FRAME").empty();
    const std::string expectedPixelHashText = getEnvironmentValue("CPPDEMO_EXPECT_PIXEL_HASH");
        const std::string fixedTimeText = getEnvironmentValue("CPPDEMO_FIXED_TIME_SECONDS");
        float fixedTimeSeconds = 0.0f;
        const bool hasFixedTime = parseFloat(fixedTimeText, fixedTimeSeconds);
        if (!fixedTimeText.empty() && !hasFixedTime) {
            std::cerr << "Fixed time invalid" << std::endl;
        }
    
    // 主循环
    while (!glfwWindowShouldClose(window)) {
        glClear(GL_COLOR_BUFFER_BIT);
        
            float currentTime = hasFixedTime ? fixedTimeSeconds : static_cast<float>(glfwGetTime());
        float rotation = currentTime * rotationSpeed;
        
        // 计算颜色
        float r = (sin(currentTime * colorSpeed) + 1.0f) * 0.5f;
        float g = (sin(currentTime * colorSpeed + 2.0f * PI / 3.0f) + 1.0f) * 0.5f;
        float b = (sin(currentTime * colorSpeed + 4.0f * PI / 3.0f) + 1.0f) * 0.5f;
        paint1.setColor(r, g, b);
        
        canvas.beginFrame();
        canvas.drawColor(Color(6, 8, 14));

        canvas.saveLayer(RectF(28.0f, 308.0f, 178.0f, 102.0f), saveLayerPaint);
        canvas.drawCircle(PointF(88.0f, 360.0f), 44.0f, layerCirclePaint);
        canvas.drawRoundRect(RectF(86.0f, 326.0f, 106.0f, 68.0f), 18.0f, 36.0f, 14.0f, 28.0f, layerRectPaint);
        canvas.restore();

        canvas.save();
        canvas.clipRect(RectF(520.0f, 420.0f, 220.0f, 150.0f));
        canvas.drawRect(RectF(500.0f, 400.0f, 260.0f, 180.0f), clipBgPaint);
        canvas.drawRect(RectF(540.0f, 440.0f, 120.0f, 120.0f), clipRedPaint);
        canvas.drawRect(RectF(600.0f, 450.0f, 120.0f, 120.0f), clipGreenPaint);
        RectF liveClipBounds;
        if (canvas.getClipBounds(liveClipBounds)) {
            canvas.drawRect(liveClipBounds, clipBoundsPaint);
        }
        if (canvas.isPointInClip(PointF(532.0f, 432.0f))
            && canvas.quickReject(RectF(760.0f, 450.0f, 40.0f, 40.0f))
            && canvas.quickReject(roundStrokePath, roundStrokePaint)) {
            canvas.drawPoint(532.0f, 432.0f, clipQueryPaint);
        }
        canvas.restore();
        canvas.drawRect(RectF(520.0f, 420.0f, 220.0f, 150.0f), clipBorderPaint);

        const int transformSave = canvas.save();
        canvas.translate(220.0f, 120.0f);
        canvas.rotate(rotation);
        canvas.scale(1.1f, 1.1f);
        const bool transformHit = canvas.hitTestPathFill(transformRectPath, PointF(220.0f, 120.0f));
        canvas.drawRect(RectF(-60.0f, -40.0f, 120.0f, 80.0f), rectPaint);
        canvas.restoreToCount(transformSave);
        transformHitPaint.setStrokeColor(transformHit ? Color(80, 255, 140) : Color(255, 80, 80));
        canvas.drawPoint(220.0f, 120.0f, transformHitPaint);

        canvas.drawPath(demoPath, pathPaint);
        canvas.drawPath(concavePath, concavePaint);
        canvas.drawPath(evenOddPath, evenOddPaint);
        canvas.drawRect(evenOddBounds, clipBorderPaint);
        canvas.drawPoint(700.0f, 280.0f, evenOddOuterHitPaint);
        canvas.drawPoint(726.0f, 281.0f, evenOddHoleHitPaint);
        canvas.drawPath(curvePath, curvePaint);
        canvas.drawTextOnPath("PATH TEXT", curvePath, 18.0f, -12.0f, pathTextPaint);
        canvas.drawPath(roundStrokePath, roundStrokePaint);
        canvas.drawRect(roundStrokeBounds, clipBoundsPaint);
        canvas.drawPoint(180.0f, 270.0f, roundStrokeHitPaint);
        canvas.drawPoint(180.0f, 235.0f, roundStrokeMissPaint);
        PointF metricPoint;
        PointF metricTangent;
        if (roundStrokeLength > 0.0f && roundStrokePath.pointAndTangentAtLength(std::fmod(currentTime * 90.0f, roundStrokeLength), metricPoint, metricTangent)) {
            const float trimStart = currentTime * 85.0f;
            const Path trimmedPath = roundStrokePath.trim(trimStart, trimStart + 180.0f, true);
            canvas.drawPath(trimmedPath, pathTrimPaint);
            const Path reverseTrimmedPath = roundStrokePath.trim(trimStart + 150.0f, trimStart, true, true);
            canvas.drawPath(reverseTrimmedPath, reversePathTrimPaint);
            canvas.drawPoint(metricPoint, pathMetricPointPaint);
            canvas.drawLine(metricPoint.getX(), metricPoint.getY(),
                            metricPoint.getX() + metricTangent.getX() * 28.0f,
                            metricPoint.getY() + metricTangent.getY() * 28.0f,
                            pathMetricTangentPaint);
        }
        canvas.drawPath(degeneratePath, roundStrokePaint);
        canvas.drawRect(RectF(500.0f, 300.0f, 190.0f, 130.0f), gradientRectPaint);
        canvas.drawRoundRect(RectF(500.0f, 80.0f, 210.0f, 150.0f), 46.0f, roundRectPaint);
        canvas.drawRoundRect(RectF(575.0f, 335.0f, 165.0f, 72.0f), 8.0f, 36.0f, 18.0f, 48.0f, asymmetricRoundRectPaint);
        canvas.drawCircle(PointF(610.0f, 505.0f), 62.0f, circlePaint);
        canvas.drawOval(RectF(80.0f, 440.0f, 220.0f, 110.0f), ovalPaint);
        canvas.drawArc(RectF(680.0f, 455.0f, 90.0f, 100.0f), -0.8f * PI, 1.45f * PI, false, arcPaint);
        canvas.drawLine(518.0f, 252.0f, 760.0f, 252.0f, dashedPaint);
        canvas.drawPolyline(demoPolylinePoints, pathPaint);
        canvas.drawPolygon(demoPolygonPoints, rectPaint);
        canvas.drawPath(polygonPath, roundedCornerPathPaint);
        if (!polygonContourBounds.empty()) {
            canvas.drawRect(polygonContourBounds.front(), clipBoundsPaint);
        }
        if (polygonPath.isClosed() && polygonPath.getClosedContourCount() == polygonPath.getContourCount()) {
            canvas.drawPath(polygonPath, closedDashPaint);
        }

        if (imageLoaded) {
            canvas.save();
            canvas.clipRect(RectF(320.0f, 20.0f, 160.0f, 120.0f));
            canvas.translate(400.0f, 80.0f);
            canvas.rotate(rotation * 0.25f);
            canvas.drawImage(demoImage, RectF(-80.0f, -60.0f, 160.0f, 120.0f), imagePaint);
            canvas.restore();

            const float srcW = static_cast<float>(demoImage.getWidth()) * 0.5f;
            const float srcH = static_cast<float>(demoImage.getHeight()) * 0.5f;
            canvas.drawImage(
                demoImage,
                RectF(0.0f, 0.0f, srcW, srcH),
                RectF(20.0f, 20.0f, 110.0f, 80.0f),
                tintedImagePaint);
            canvas.drawImage(
                demoImage,
                RectF(0.0f, 0.0f, srcW, srcH),
                RectF(140.0f, 20.0f, 110.0f, 80.0f),
                filteredImagePaint);
            canvas.drawImageNinePatch(
                demoImage,
                RectF(srcW * 0.45f, srcH * 0.45f, srcW * 0.6f, srcH * 0.6f),
                RectF(20.0f, 112.0f, 160.0f, 44.0f),
                imagePaint);
            canvas.drawImageFit(demoImage, RectF(190.0f, 112.0f, 58.0f, 44.0f), Canvas::ImageFit::COVER, Canvas::ImageAnchor::TOP, filteredImagePaint);
            canvas.drawImageFit(demoImage, RectF(252.0f, 112.0f, 52.0f, 44.0f), Canvas::ImageFit::CONTAIN, 1.0f, 0.0f, imagePaint);
            canvas.drawImageTiled(demoImage, RectF(260.0f, 20.0f, 44.0f, 80.0f), 16.0f, 16.0f, tiledImagePaint);
            canvas.drawImageTiled(demoImage, RectF(310.0f, 20.0f, 44.0f, 80.0f), 16.0f, 16.0f, decalImagePaint);
        }

            canvas.drawTextBox("Batch74: drawTextBox wraps ASCII text, caps visible rows, and ellipsizes overflow for compact panels.",
                               RectF(24.0f, 562.0f, 330.0f, 34.0f), 14.0f, 2, true, textPaint);

            canvas.save();
            canvas.translate(560.0f, 260.0f);
            canvas.rotate(-rotation * 0.35f);
            const RectF rotatingTextClipRect(-rotatingTextClipHalfWidth, -rotatingTextClipHeight * 0.5f,
                                             rotatingTextClipHalfWidth * 2.0f, rotatingTextClipHeight);
            const RectF rotatingTextDeviceBounds = canvas.mapRect(rotatingTextClipRect);
            canvas.clipRect(rotatingTextClipRect);
            canvas.drawText("Rotating Text", 0.0f, 10.0f, rotatingTextPaint);
            canvas.restore();
            canvas.drawRect(rotatingTextDeviceBounds, clipBoundsPaint);
        
        // 计算并存储顶点
        std::vector<std::pair<float, float>> points;
        for (int i = 0; i < numPoints; i++) {
            float angle = rotation + i * (2 * PI / numPoints);
            float x = centerX + radius * cos(angle);
            float y = centerY + radius * sin(angle);
            points.push_back({x, y});
        }
        
        // 绘制线条
        for (int i = 0; i < numPoints; i++) {
            int next = (i + 2) % numPoints;
            canvas.drawLine(
                points[i].first, points[i].second,
                points[next].first, points[next].second,
                paint1
            );
            canvas.drawPoint(points[i].first, points[i].second, paint1);
        }
        
        canvas.endFrame();
        if (!pixelReadbackChecked) {
            std::vector<unsigned char> pixels;
            const bool readbackOk = canvas.readPixelsRGBA(pixels);
            const size_t expectedPixelBytes = static_cast<size_t>(canvas.getWidth()) * static_cast<size_t>(canvas.getHeight()) * 4;
            if (!readbackOk || pixels.size() != expectedPixelBytes) {
                std::cerr << "Pixel readback failed" << std::endl;
            } else {
                const std::uint64_t bufferHash = Canvas::hashPixelsRGBA(pixels);
                const std::uint64_t framebufferHash = canvas.computePixelsHashRGBA();
                if (bufferHash != framebufferHash) {
                    std::cerr << "Pixel hash mismatch" << std::endl;
                }
                if (!expectedPixelHashText.empty()) {
                    std::uint64_t expectedPixelHash = 0;
                    if (!parseUint64(expectedPixelHashText, expectedPixelHash)) {
                        std::cerr << "Pixel hash expectation invalid" << std::endl;
                    } else if (bufferHash != expectedPixelHash) {
                        std::cerr << "Pixel hash mismatch expected=" << expectedPixelHash
                                  << " actual=" << bufferHash << std::endl;
                    }
                }
                if (printPixelHash) {
                    std::cout << "PIXEL_HASH_RGBA=" << bufferHash << std::endl;
                }
            }
            pixelReadbackChecked = true;
        }
        if (!captureChecked && !capturePath.empty()) {
            if (!canvas.savePixelsPPM(capturePath)) {
                std::cerr << "PPM capture failed" << std::endl;
            }
            captureChecked = true;
        }
        if (exitAfterFirstFrame && pixelReadbackChecked && (captureChecked || capturePath.empty())) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        
        glfwSwapBuffers(window);
        glfwPollEvents();
    }
    
    canvas.finalize();

    // 终止 GLFW
    glfwTerminate();
    std::cout << "GLFW terminated. Exiting application." << std::endl;

    return 0;
}