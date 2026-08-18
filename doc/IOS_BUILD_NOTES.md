# WhatsCanvas iOS Build Notes

The repository ships a UIKit sample at `platforms/ios/` using Metal for
rendering and CoreText for native text. The iOS path does not require or link
OpenGL ES.

## Supported configuration

- Xcode 26.6 and the iOS 26.5 SDK
- iOS 15.0 deployment target
- iPhone and iPad, portrait and landscape
- Bundle identifier `con.whatscanvas.demo`
- C++17, Objective-C++, UIKit, Metal, QuartzCore, CoreGraphics, and CoreText

Apple's Xcode 26.6 support matrix still permits iOS 15 deployment, while the
2026 App Store toolchain requires the iOS 26 SDK. The project therefore keeps
the broad iOS 15 runtime floor and builds with the current Xcode 26 toolchain.

## Build model

The Xcode build phase calls `platforms/ios/scripts/build_whatscanvas.sh`. It
configures only `WhatsCanvas::Metal` through `WHATSCANVAS_BUILD_METAL=ON` and
turns off the OpenGL, OpenGL ES, Vulkan, Software, demo, benchmark, and portable
font dependency targets. CMake selects the active simulator/device SDK and
architecture supplied by Xcode.

The host creates a `CAMetalLayer`, initializes `Canvas::Backend::Metal`, selects
`Canvas::TextBackend::CoreText`, and hands the layer to
`OutputTarget::ToWindow`. The display link requests a fixed 60 Hz range and
uses three swapchain images.

## Lifecycle contract

On resize, safe-area change, or orientation change, the host recreates the
canvas for the new drawable size and re-records static content. On background,
it stops `CADisplayLink` and releases the Metal canvas, images, retained
picture, and swapchain. Foreground activation reconstructs those resources
before the frame loop resumes.

`WhatsCanvasDemoUITests` verifies portrait, landscape, background/resume, and
a terminate/relaunch cold start. It keeps portrait and landscape screenshots in
the Xcode result bundle.

## Commands

```sh
xcodebuild \
  -project platforms/ios/WhatsCanvasDemo.xcodeproj \
  -scheme WhatsCanvasDemo \
  -configuration Debug \
  -destination 'platform=iOS Simulator,name=iPhone 17 Pro' \
  CODE_SIGNING_ALLOWED=NO \
  build

xcodebuild test \
  -project platforms/ios/WhatsCanvasDemo.xcodeproj \
  -scheme WhatsCanvasDemo \
  -configuration Debug \
  -destination 'platform=iOS Simulator,name=iPhone 17 Pro' \
  CODE_SIGNING_ALLOWED=NO
```

A generic device compile uses `-sdk iphoneos -destination
'generic/platform=iOS'`. Simulator checks do not replace final physical-device
profiling, thermal, memory-pressure, signing, and distribution validation.
