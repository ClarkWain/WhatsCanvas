#import "MetalCanvasView.h"

#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>
#import <ImageIO/ImageIO.h>
#import <MobileCoreServices/MobileCoreServices.h>

#include "DemoScene.h"
#include "../../shared/scenes/CanonicalViewport.h"
#include "../../shared/scenes/StressScenes.h"

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
    BOOL _screenshotCaptureEnabled;
    BOOL _fixedCaptureTimeEnabled;
    float _fixedCaptureTimeSeconds;
    BOOL _applicationActive;
    whatscanvas::scenes::StressSceneId _stressSceneId;
    BOOL _stressSceneEnabled;
    NSString *_sceneID;
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
        // Transaction-backed presentation is required on current physical
        // iOS devices. Simulator presentation also supports this path.
        layer.presentsWithTransaction = YES;
        _screenshotCaptureEnabled =
            [NSProcessInfo.processInfo.arguments containsObject:@"--capture-frames"];
        _sceneID = @"feature_showcase";
        for (NSString *argument in NSProcessInfo.processInfo.arguments) {
            static NSString *const prefix = @"--capture-time=";
            if ([argument hasPrefix:prefix]) {
                const double value = [[argument substringFromIndex:prefix.length] doubleValue];
                if (std::isfinite(value) && value >= 0.0) {
                    _fixedCaptureTimeEnabled = YES;
                    _fixedCaptureTimeSeconds = static_cast<float>(value);
                }
            }
            static NSString *const scenePrefix = @"--capture-scene=";
            if ([argument hasPrefix:scenePrefix]) {
                NSString *value = [argument substringFromIndex:scenePrefix.length];
                whatscanvas::scenes::StressSceneId parsed;
                if (whatscanvas::scenes::parseStressScene(value.UTF8String, parsed)) {
                    _stressSceneId = parsed;
                    _stressSceneEnabled = YES;
                    _sceneID = value;
                }
            }
        }
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
    if (!_canvas->setTextBackend(wsc::Canvas::TextBackend::CoreText)) {
        NSLog(@"WhatsCanvas: CoreText backend initialization failed");
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

    const float logicalWidth = pixelWidth / scale;
    const float logicalHeight = pixelHeight / scale;
    const float safeTop = static_cast<float>(self.safeAreaInsets.top);
    const float safeBottom = static_cast<float>(self.safeAreaInsets.bottom);
    const float safeLeft = static_cast<float>(self.safeAreaInsets.left);
    const float safeRight = static_cast<float>(self.safeAreaInsets.right);
    if (!_stressSceneEnabled) {
        whatscanvas::demo::createCheckerImage(*_canvas, _checkerImage);
        _staticPicture = whatscanvas::demo::recordStaticScene(
            *_canvas, logicalWidth, logicalHeight, safeTop, safeBottom,
            safeLeft, safeRight);
    }
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
    const float elapsed = _fixedCaptureTimeEnabled
        ? _fixedCaptureTimeSeconds
        : static_cast<float>(displayLink.timestamp - _startTime);

    _canvas->beginFrame();
    if (_stressSceneEnabled) {
        _canvas->drawColor(wsc::Color(7, 11, 27));
        const auto viewport = whatscanvas::scenes::makeCanonicalViewport(
            width, height, {safeTop, safeBottom, safeLeft, safeRight});
        _canvas->save();
        _canvas->translate(viewport.offsetX, viewport.offsetY);
        _canvas->scale(viewport.scale, viewport.scale);
        whatscanvas::scenes::drawStressScene(
            *_canvas, _stressSceneId, viewport.width, viewport.height, elapsed);
        _canvas->restore();
    } else {
        if (_staticPicture != nullptr) {
            _canvas->drawPictureRasterized(*_staticPicture);
        }
        whatscanvas::demo::drawDynamicScene(*_canvas, _checkerImage.get(),
                                            width, height, safeTop, safeBottom,
                                            safeLeft, safeRight, elapsed);
    }
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
    if (_screenshotCaptureEnabled && (_screenshotCounter++ % 120u) == 0u) {
        NSString *filename = @"screenshot.png";
        if (_fixedCaptureTimeEnabled) {
            const NSString *viewportID = width > height
                ? @"landscape" : @"portrait";
            const NSString *sampleID = [NSString stringWithFormat:@"t%04d",
                static_cast<int>(std::lround(_fixedCaptureTimeSeconds * 1000.0f))];
            filename = [NSString stringWithFormat:@"%@-%@-%@.png",
                _sceneID, viewportID, sampleID];
        }
        [self dumpScreenshotAsPNG:filename];
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
    if (ok) {
        const float scale = static_cast<float>(self.contentScaleFactor);
        const float logicalWidth = pixelWidth / scale;
        const float logicalHeight = pixelHeight / scale;
        const auto viewport = whatscanvas::scenes::makeCanonicalViewport(
            logicalWidth, logicalHeight,
            {static_cast<float>(self.safeAreaInsets.top),
             static_cast<float>(self.safeAreaInsets.bottom),
             static_cast<float>(self.safeAreaInsets.left),
             static_cast<float>(self.safeAreaInsets.right)});
        const NSArray<NSNumber *> *contentRect = @[
            @(static_cast<int>(std::lround(viewport.offsetX * scale))),
            @(static_cast<int>(std::lround(viewport.offsetY * scale))),
            @(static_cast<int>(std::lround(viewport.width * viewport.scale * scale))),
            @(static_cast<int>(std::lround(viewport.height * viewport.scale * scale)))
        ];
        const NSString *sampleID = _fixedCaptureTimeEnabled
            ? [NSString stringWithFormat:@"t%04d",
                static_cast<int>(std::lround(_fixedCaptureTimeSeconds * 1000.0f))]
            : @"live";
        const NSDictionary *metadata = @{
            @"schema_version": @1,
            @"scene_id": _sceneID,
            @"viewport_id": viewport.width > viewport.height
                ? @"landscape" : @"portrait",
            @"sample_id": sampleID,
            @"platform": @"ios",
            @"backend": @"metal",
            @"elapsed_seconds": @(_fixedCaptureTimeEnabled
                ? _fixedCaptureTimeSeconds : -1.0f),
            @"content_rect_pixels": contentRect
        };
        NSError *metadataError = nil;
        NSData *metadataData = [NSJSONSerialization dataWithJSONObject:metadata
                                                               options:NSJSONWritingPrettyPrinted
                                                                 error:&metadataError];
        NSString *metadataPath = [[fullPath stringByDeletingPathExtension]
            stringByAppendingPathExtension:@"json"];
        if (metadataData == nil || ![metadataData writeToFile:metadataPath
                                                      options:NSDataWritingAtomic
                                                        error:&metadataError]) {
            NSLog(@"WhatsCanvas: screenshot metadata failed: %@", metadataError);
        }
    }
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
