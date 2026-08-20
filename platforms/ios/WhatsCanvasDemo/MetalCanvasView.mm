#import "MetalCanvasView.h"

#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#import <ImageIO/ImageIO.h>
#import <MobileCoreServices/MobileCoreServices.h>

#include "DemoScene.h"

#include <wsc/wsc.h>

#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

@interface MetalCanvasView ()
@property(nonatomic, strong) CADisplayLink *displayLink;
@end

@implementation MetalCanvasView {
    std::unique_ptr<wsc::Canvas> _canvas;
    std::unique_ptr<wsc::Image> _checkerImage;
    std::shared_ptr<const wsc::Picture> _staticPicture;
    CGSize _drawableSize;
    CFTimeInterval _startTime;
    CFTimeInterval _fpsWindowStart;
    NSUInteger _fpsFrameCount;
    NSUInteger _screenshotCounter;
    BOOL _applicationActive;
}

+ (Class)layerClass
{
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self != nil) {
        self.opaque = YES;
        self.isAccessibilityElement = YES;
        self.accessibilityIdentifier = @"whatscanvas.canvas";
        self.accessibilityLabel = @"WhatsCanvas Metal canvas";
        self.accessibilityValue = @"initializing";
        self.contentScaleFactor = UIScreen.mainScreen.scale;
        CAMetalLayer *layer = (CAMetalLayer *)self.layer;
        layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer.framebufferOnly = YES;
        layer.opaque = YES;
        layer.contentsScale = UIScreen.mainScreen.scale;
        _applicationActive = YES;
        _startTime = CACurrentMediaTime();
        _fpsWindowStart = _startTime;
        NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
        [center addObserver:self selector:@selector(applicationDidEnterBackground:)
                       name:UIApplicationDidEnterBackgroundNotification object:nil];
        [center addObserver:self selector:@selector(applicationWillEnterForeground:)
                       name:UIApplicationWillEnterForegroundNotification object:nil];
        [center addObserver:self selector:@selector(applicationDidBecomeActive:)
                       name:UIApplicationDidBecomeActiveNotification object:nil];
        [center addObserver:self selector:@selector(applicationWillResignActive:)
                       name:UIApplicationWillResignActiveNotification object:nil];
    }
    return self;
}

- (void)dealloc
{
    [NSNotificationCenter.defaultCenter removeObserver:self];
    [self stopDisplayLink];
    [self releaseRenderer];
}

- (void)didMoveToWindow
{
    [super didMoveToWindow];
    if (self.window != nil && _applicationActive) {
        [self rebuildRendererIfNeeded];
        [self startDisplayLink];
    } else {
        [self stopDisplayLink];
    }
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    const CGFloat scale = self.window.screen.scale ?: UIScreen.mainScreen.scale;
    self.contentScaleFactor = scale;
    const CGSize size = CGSizeMake(std::max(1.0, self.bounds.size.width * scale),
                                   std::max(1.0, self.bounds.size.height * scale));
    ((CAMetalLayer *)self.layer).drawableSize = size;
    if (!CGSizeEqualToSize(size, _drawableSize)) {
        _drawableSize = size;
        [self rebuildRenderer];
    }
}

- (void)safeAreaInsetsDidChange
{
    [super safeAreaInsetsDidChange];
    if (self.window != nil) [self rebuildRenderer];
}

- (void)startDisplayLink
{
    if (self.displayLink != nil || self.window == nil || !_applicationActive) return;
    self.displayLink = [CADisplayLink displayLinkWithTarget:self
                                                   selector:@selector(drawFrame:)];
    self.displayLink.preferredFrameRateRange = CAFrameRateRangeMake(60.0, 60.0, 60.0);
    [self.displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (void)stopDisplayLink
{
    [self.displayLink invalidate];
    self.displayLink = nil;
}

- (void)rebuildRendererIfNeeded
{
    if (_canvas == nullptr) [self rebuildRenderer];
}

- (void)rebuildRenderer
{
    self.accessibilityValue = @"initializing";
    [self releaseRenderer];
    if (self.window == nil || _drawableSize.width < 1.0 || _drawableSize.height < 1.0) return;

    const int pixelWidth = static_cast<int>(std::lround(_drawableSize.width));
    const int pixelHeight = static_cast<int>(std::lround(_drawableSize.height));
    const float scale = static_cast<float>(self.contentScaleFactor);
    _canvas = wsc::Canvas::create(wsc::Canvas::Backend::Metal,
                                  pixelWidth, pixelHeight);
    if (_canvas == nullptr) {
        NSLog(@"WhatsCanvas: Metal backend unavailable");
        return;
    }
    _canvas->setDevicePixelRatio(scale);
    if (!_canvas->initializeContext()) {
        NSLog(@"WhatsCanvas: Metal context initialization failed");
        _canvas.reset();
        return;
    }
    wsc::Canvas::TextBackend textBackend = wsc::Canvas::TextBackend::CoreText;
#if TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR
    textBackend = wsc::Canvas::TextBackend::Portable;
#endif
    if (!_canvas->setTextBackend(textBackend)) {
        NSLog(@"WhatsCanvas: text backend initialization failed");
        [self releaseRenderer];
        return;
    }

    wsc::NativeSurface surface;
    surface.platform = wsc::NativeSurface::Platform::Cocoa;
    surface.window = (__bridge void *)((CAMetalLayer *)self.layer);
    wsc::SwapchainConfig swapchain;
    swapchain.vsync = YES;
    swapchain.imageCount = 3;
    if (!_canvas->setOutputTarget(wsc::OutputTarget::ToWindow(surface, swapchain))) {
        NSLog(@"WhatsCanvas: CAMetalLayer swapchain creation failed");
        [self releaseRenderer];
        return;
    }

    whatscanvas::demo::createCheckerImage(*_canvas, _checkerImage);
    const float logicalWidth = pixelWidth / scale;
    const float logicalHeight = pixelHeight / scale;
    const float safeTop = static_cast<float>(self.safeAreaInsets.top);
    const float safeBottom = static_cast<float>(self.safeAreaInsets.bottom);
    const float safeLeft = static_cast<float>(self.safeAreaInsets.left);
    const float safeRight = static_cast<float>(self.safeAreaInsets.right);
    _staticPicture = whatscanvas::demo::recordStaticScene(
        *_canvas, logicalWidth, logicalHeight, safeTop, safeBottom,
        safeLeft, safeRight);
    _startTime = CACurrentMediaTime();
    _fpsWindowStart = _startTime;
    _fpsFrameCount = 0;
    self.accessibilityValue = @"ready";
    NSLog(@"WhatsCanvas: Metal + CoreText ready at %dx%d (%.2fx)",
          pixelWidth, pixelHeight, scale);
}

- (void)releaseRenderer
{
    self.accessibilityValue = @"released";
    _staticPicture.reset();
    _checkerImage.reset();
    if (_canvas != nullptr) {
        _canvas->finalizeContext();
        _canvas.reset();
    }
}

- (void)drawFrame:(CADisplayLink *)displayLink
{
    if (_canvas == nullptr || !_applicationActive) return;
    const float scale = static_cast<float>(self.contentScaleFactor);
    const float width = static_cast<float>(_drawableSize.width) / scale;
    const float height = static_cast<float>(_drawableSize.height) / scale;
    const float safeTop = static_cast<float>(self.safeAreaInsets.top);
    const float safeBottom = static_cast<float>(self.safeAreaInsets.bottom);
    const float safeLeft = static_cast<float>(self.safeAreaInsets.left);
    const float safeRight = static_cast<float>(self.safeAreaInsets.right);
    const float elapsed = static_cast<float>(displayLink.timestamp - _startTime);

    _canvas->beginFrame();
    if (_staticPicture != nullptr) {
        _canvas->drawPictureRasterized(*_staticPicture);
    }
    whatscanvas::demo::drawDynamicScene(*_canvas, _checkerImage.get(),
                                        width, height, safeTop, safeBottom,
                                        safeLeft, safeRight, elapsed);
    _canvas->endFrame();
    if (!_canvas->present()) {
        NSLog(@"WhatsCanvas: Metal present failed");
    }

    ++_fpsFrameCount;
    const CFTimeInterval now = CACurrentMediaTime();
    const CFTimeInterval duration = now - _fpsWindowStart;
    if (duration >= 5.0) {
        const double fps = static_cast<double>(_fpsFrameCount) / duration;
        const auto stats = _canvas->getRenderStats();
        NSLog(@"WhatsCanvas: %.1f fps, %zu draws, %zu commands, gpu %.2f ms",
              fps, stats.drawCallCount, stats.commandCount,
              stats.gpuTimeAvailable ? stats.gpuTimeNs / 1000000.0 : 0.0);
        _fpsWindowStart = now;
        _fpsFrameCount = 0;
    }

    // C/S screenshot: dump the frame's source texture (mainTarget, i.e. what
    // presentFragment samples to the drawable) as PNG so the Mac can pull it
    // via `xcrun devicectl device copy from --domain-type appDataContainer`.
    if ((_screenshotCounter++ % 120u) == 0u) {
        [self dumpScreenshotAsPNG:@"screenshot.png"];
    }
}

- (BOOL)dumpScreenshotAsPNG:(NSString *)filename
{
    if (_canvas == nullptr) return NO;
    std::vector<unsigned char> pixels;
    if (!_canvas->readPixelsRGBA(pixels) || pixels.empty()) {
        NSLog(@"WhatsCanvas: screenshot readPixelsRGBA failed");
        return NO;
    }
    const int pixelWidth = static_cast<int>(std::lround(_drawableSize.width));
    const int pixelHeight = static_cast<int>(std::lround(_drawableSize.height));
    const std::size_t expected =
        static_cast<std::size_t>(pixelWidth) * static_cast<std::size_t>(pixelHeight) * 4u;
    if (pixels.size() < expected) {
        NSLog(@"WhatsCanvas: screenshot bytes %zu < expected %zu", pixels.size(), expected);
        return NO;
    }
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, pixels.data(),
                                  static_cast<CFIndex>(expected));
    CGDataProviderRef provider = CGDataProviderCreateWithCFData(data);
    CGBitmapInfo info = kCGBitmapByteOrder32Big | kCGImageAlphaPremultipliedLast;
    CGImageRef image = CGImageCreate(pixelWidth, pixelHeight, 8, 32, pixelWidth * 4,
                                     colorSpace, info, provider,
                                     NULL, false, kCGRenderingIntentDefault);
    NSString *docsDir = NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSString *fullPath = [docsDir stringByAppendingPathComponent:filename];
    NSURL *url = [NSURL fileURLWithPath:fullPath];
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL(
        (__bridge CFURLRef)url, (__bridge CFStringRef)@"public.png", 1, NULL);
    BOOL ok = NO;
    if (dest && image) {
        CGImageDestinationAddImage(dest, image, NULL);
        ok = CGImageDestinationFinalize(dest);
    }
    if (dest) CFRelease(dest);
    if (image) CGImageRelease(image);
    if (provider) CGDataProviderRelease(provider);
    if (data) CFRelease(data);
    if (colorSpace) CGColorSpaceRelease(colorSpace);
    NSLog(@"WhatsCanvas: screenshot %s -> %@ (%dx%d)",
          ok ? "OK" : "FAILED", fullPath, pixelWidth, pixelHeight);
    return ok;
}

- (void)applicationWillResignActive:(NSNotification *)notification
{
    (void)notification;
    _applicationActive = NO;
    [self stopDisplayLink];
    NSLog(@"WhatsCanvas: display link paused");
}

- (void)applicationDidEnterBackground:(NSNotification *)notification
{
    (void)notification;
    [self releaseRenderer];
    NSLog(@"WhatsCanvas: background renderer released");
}

- (void)applicationWillEnterForeground:(NSNotification *)notification
{
    (void)notification;
    [self setNeedsLayout];
}

- (void)applicationDidBecomeActive:(NSNotification *)notification
{
    (void)notification;
    _applicationActive = YES;
    [self rebuildRendererIfNeeded];
    [self startDisplayLink];
}

@end
