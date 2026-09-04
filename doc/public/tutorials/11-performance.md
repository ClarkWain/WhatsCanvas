# Chapter 11: Performance Optimization

> Goal of this chapter: master the WhatsCanvas performance toolkit — Picture recording and rasterized cache, quickReject, render stats, resource management, and benchmarking.

For the Chinese version, see [`zh/11-performance.md`](./zh/11-performance.md).

---

## 11.1 The Levels of Performance Optimization

```
Application    → Reduce unnecessary drawing (quickReject, dirty regions)
Cache          → Picture recording + rasterized cache (avoid recomputation)
Resource       → Texture management, mipmaps, atlas
Backend        → Choose an appropriate backend, reduce state changes
```

---

## 11.2 Picture Recording

`Picture` records a sequence of draw commands into an immutable "replay" that can be re-played many times:

```cpp
// Record draw commands (they do not run immediately)
auto picture = canvas->recordPicture([](wsc::Canvas& c) {
    wsc::Paint bg;
    bg.setLinearGradient(0, 0, 400, 300,
        wsc::Color(30, 60, 120, 255), wsc::Color(20, 20, 40, 255));
    c.drawRect(wsc::RectF(0, 0, 400, 300), bg);

    wsc::Paint text;
    text.setColor(wsc::Color(255, 255, 255, 255));
    text.setTextSize(24.0f);
    c.drawText("Static Content", 50, 150, text);

    // ... lots of static drawing ...
});

// Replay many times — faster than re-executing every draw command
canvas->drawPicture(*picture);
```

### When to Use

- Static parts of the UI that do not change per frame (background, toolbar, sidebar)
- Complex icons or decorations
- Cached results of complex path drawing

### Picture Properties

```cpp
picture->operationCount();  // Number of recorded draw commands
picture->empty();           // Is it empty?
```

---

## 11.3 Rasterized Cache (Rasterized Picture)

`drawPictureRasterized` goes one step further — it caches the Picture's output into a GPU texture, so subsequent frames just blit the texture:

```cpp
// First call: execute the Picture and cache into a GPU texture.
// Subsequent calls: use the cached texture, skip every draw command.
canvas->drawPictureRasterized(*picture);
```

### Cache Budget

```cpp
// Set the total cache budget (bytes)
canvas->setRetainedPictureRasterCacheBudgetBytes(64 * 1024 * 1024);  // 64MB

// Query the current budget
size_t budget = canvas->retainedPictureRasterCacheBudgetBytes();
```

### drawPicture vs. drawPictureRasterized

| Aspect | `drawPicture` | `drawPictureRasterized` |
|--------|:------------:|:----------------------:|
| First-time cost | Low (just replay commands) | High (render + upload texture) |
| Subsequent cost | Medium (replay each time) | Very low (texture blit) |
| Memory | Very low | High (cached texture) |
| Transform support | Perfect (vector replay) | Scaling causes blur |
| When to use | Few commands or requires scaling | Many commands, fixed size |

---

## 11.4 Quick Reject

In a heavy drawing loop, `quickReject` quickly decides whether a region is fully invisible without rendering it:

```cpp
// Great for scrolling lists, large canvases, ...
void drawItems(wsc::Canvas& canvas, const std::vector<Item>& items) {
    for (const auto& item : items) {
        wsc::RectF bounds = item.getBounds();

        // Fully outside the clip? Skip.
        if (canvas.quickReject(bounds)) {
            continue;
        }

        item.draw(canvas);
    }
}
```

### Combine with Clipping

```cpp
// Clip to the viewport
canvas->clipRect(viewportRect);

// Only draw what intersects the viewport
for (const auto& widget : widgets) {
    if (!canvas->quickReject(widget.bounds())) {
        widget.render(*canvas);
    }
}
```

---

## 11.5 Reduce State Changes

### Reuse Paints

```cpp
// Bad: create a new Paint per element
for (auto& item : items) {
    wsc::Paint p;              // Constructed + destroyed each time
    p.setColor(item.color);
    p.setTextSize(14.0f);
    p.setAntiAlias(true);
    canvas->drawText(item.text, item.x, item.y, p);
}

// Good: reuse the Paint, only mutate what changes
wsc::Paint p;
p.setTextSize(14.0f);
p.setAntiAlias(true);
for (auto& item : items) {
    p.setColor(item.color);    // Only the color varies
    canvas->drawText(item.text, item.x, item.y, p);
}
```

### Batch Same-Kind Draws

```cpp
// Group draws that share Paint attributes.
// GPU backends benefit from fewer shader / state changes.

// Draw all backgrounds first
wsc::Paint bgPaint;
bgPaint.setColor(wsc::Color(240, 240, 240, 255));
for (auto& card : cards) {
    canvas->drawRoundRect(card.bgRect, 12, bgPaint);
}

// Then all text
wsc::Paint textPaint;
textPaint.setTextSize(14.0f);
textPaint.setColor(wsc::Color(33, 33, 33, 255));
for (auto& card : cards) {
    canvas->drawText(card.title, card.titleX, card.titleY, textPaint);
}
```

---

## 11.6 Image Resource Optimization

### Mipmaps

Mipmaps significantly reduce aliasing and improve performance when drawing downscaled images:

```cpp
// Generate mipmaps at load time (on by default)
canvas->loadImageFromEncodedMemory(image, data, size, true /*generateMipmaps*/);

// Use mipmap sampling when drawing
wsc::Paint imgPaint;
imgPaint.setImageSampling(wsc::Paint::ImageSampling::MIPMAP_LINEAR);
canvas->drawImageFit(image, dst, wsc::Canvas::ImageFit::COVER, imgPaint);
```

### Do Not Load Every Frame

```cpp
// Bad: create a texture every frame
void onFrame(Canvas& canvas) {
    Image img;
    canvas.loadImage(img, "icon.png");  // Uploaded per frame!
    canvas.drawImage(img, 10, 10, paint);
}

// Good: load at init, keep the reference
class MyScene {
    Image icon_;

    void onInit(Canvas& canvas) {
        canvas.loadImage(icon_, "icon.png");
    }

    void onFrame(Canvas& canvas) {
        canvas.drawImage(icon_, 10, 10, paint);
    }
};
```

### Partial Updates

If only a small region of the image changes, do a partial update instead of replacing everything:

```cpp
// Only update the changed region
canvas->updateImageRGBA(image, dirtyPixels, dirtyX, dirtyY, dirtyW, dirtyH);
```

---

## 11.7 Render Stats

WhatsCanvas ships built-in performance stats:

```cpp
// Enable GPU timing
canvas->setGpuTimingEnabled(true);

// Get stats after endFrame
canvas->endFrame();
auto stats = canvas->getRenderStats();
```

The fields in `RenderStats` (see `CanvasStats.h`) help you locate bottlenecks.

---

## 11.8 Async Pixel Readback

Synchronous readback (`readPixelsRGBA`) stalls the GPU pipeline. When the readback is not immediately needed, use the async version:

```cpp
// Fire an async read request
canvas->readPixelsRGBAAsync([](std::vector<unsigned char> pixels, int w, int h) {
    // The callback fires when the data is ready.
    // Process pixels on a background thread (save file, upload, ...).
    savePNG(pixels, w, h, "screenshot.png");
});

// Poll for completion in later frames
if (canvas->hasPendingReadPixelsRGBAAsync()) {
    canvas->pollReadPixelsRGBAAsync();
}
```

---

## 11.9 Frame-Loop Optimization Patterns

### Dirty Region Tracking

If only part of the UI changes, redraw only that region:

```cpp
void renderFrame(Canvas& canvas, const DirtyRegion& dirty) {
    if (dirty.isEmpty()) {
        // Nothing to redraw
        return;
    }

    canvas.beginFrame();

    // Clip to the dirty region
    canvas.save();
    canvas.clipRect(dirty.bounds());

    // Redraw anything intersecting the dirty region
    for (auto& widget : widgets_) {
        if (!canvas.quickReject(widget.bounds())) {
            widget.render(canvas);
        }
    }

    canvas.restore();
    canvas.endFrame();
}
```

### Conditional Frame Updates

For non-animated scenes, only render new frames when something changes:

```cpp
bool needsRedraw = false;

void onUserInput() {
    needsRedraw = true;
}

void mainLoop() {
    while (!shouldQuit) {
        pollEvents();

        if (needsRedraw || hasAnimation) {
            canvas->beginFrame();
            draw(*canvas);
            canvas->endFrame();
            canvas->present();
            needsRedraw = false;
        } else {
            // No changes; wait for events (save power)
            waitEvents();
        }
    }
}
```

---

## 11.10 Benchmarking

### Use the Desktop Benchmark Mode

The repository ships a benchmark tool:

```bash
# Run benchmark (30 warmup frames + 300 measured)
./build/whatscanvas_desktop --scene=feature_showcase --benchmark

# Custom frame counts
./build/whatscanvas_desktop --scene=geometry_stress \
    --benchmark --warmup=60 --measured=600
```

### Custom Benchmark

```cpp
#include <chrono>

void benchmark(Canvas& canvas, int warmupFrames, int measuredFrames) {
    // Warm up
    for (int i = 0; i < warmupFrames; ++i) {
        canvas.beginFrame();
        drawScene(canvas);
        canvas.endFrame();
    }

    // Measure
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < measuredFrames; ++i) {
        canvas.beginFrame();
        drawScene(canvas);
        canvas.endFrame();
    }
    auto end = std::chrono::high_resolution_clock::now();

    float totalMs = std::chrono::duration<float, std::milli>(end - start).count();
    float avgMs = totalMs / measuredFrames;
    float fps = 1000.0f / avgMs;

    printf("Average frame time: %.2f ms (%.0f FPS)\n", avgMs, fps);
}
```

### Pixel Hash Regression

Ensure your optimizations do not change the output:

```cpp
canvas->endFrame();
uint64_t hash = canvas->computePixelsHashRGBA();
assert(hash == expectedHash && "Pixel regression detected!");
```

---

## 11.11 Cache Strategies for Interactive Scenes

Dragging, zooming, and animation trigger continuous redraws. Beyond reducing draw commands, you also need to choose a caching strategy that matches how often content changes.

### Stable vs. Dynamic Layers

Split the UI into two layers:

```text
Stable layer:  Background, fixed toolbar, unchanged list or canvas content
Dynamic layer: Drag target, selection highlight, press feedback, animations
```

The stable layer is a good fit for Picture or Image caches; the dynamic layer is usually drawn directly and placed above the stable layer. That way, when a dynamic object moves, you do not need to regenerate the whole background cache.

```cpp
drawStableContent(canvas);   // Replay stable content
drawMovingItem(canvas);      // Draw the current-frame dynamic object
drawInteractionEffect(canvas);
```

### Choose the Right Cache Type

Picture, Image, and Atlas solve different problems:

| Scenario | Recommended |
|----------|-------------|
| Complex vector content redrawn frequently | `Picture` |
| Complex content, fixed size, long-lived | `drawPictureRasterized` |
| Background or decoration that can be stable pixels | `Image` |
| Lots of repeated icons, pieces, or particles | Image Atlas |
| Content that changes every frame or keeps scaling | Direct draw or plain `drawPicture` |

`Picture` keeps vector replay ability — pick it when you still need scaling or transform. `drawPictureRasterized` takes extra texture memory, but avoids re-executing heavy commands in later frames. For large static gradients or decoration you can also render them once into a Software Canvas as a bitmap and load them as an `Image`.

The example below uses an offscreen Canvas to draw a background, then uploads the pixels to the target Canvas as a reusable Image:

```cpp
std::unique_ptr<wsc::Image> createBackgroundImage(wsc::Canvas& targetCanvas,
                                                   int width, int height) {
    // A Software Canvas is used to generate pixels once, without taking budget from the target frame.
    auto scratch = wsc::Canvas::create(
        wsc::Canvas::Backend::Software, width, height);
    if (!scratch || !scratch->initializeContext()) {
        return nullptr;
    }

    scratch->beginFrame();

    wsc::Paint background;
    background.setRadialGradient(
        width * 0.5f, height * 0.5f, width * 0.7f,
        wsc::Color(42, 132, 88), wsc::Color(8, 48, 34));
    scratch->drawRect(
        wsc::RectF(0, 0, static_cast<float>(width),
                   static_cast<float>(height)),
        background);

    // You can also add shadows, decorative lines, and other static content here.
    scratch->endFrame();

    std::vector<unsigned char> pixels;
    const bool readOk = scratch->readPixelsRGBA(pixels);
    scratch->finalizeContext();
    if (!readOk) {
        return nullptr;
    }

    auto image = std::make_unique<wsc::Image>();
    if (!targetCanvas.loadImageFromRGBA(
            *image, pixels, width, height,
            false /* generateMipmaps */)) {
        return nullptr;
    }
    return image;
}
```

Once created, draw the Image directly in the frame loop:

```cpp
if (backgroundImage) {
    wsc::Paint imagePaint;
    imagePaint.setColor(wsc::Color::WHITE);
    canvas.drawImage(
        *backgroundImage,
        wsc::RectF(0, 0,
                   static_cast<float>(backgroundImage->getWidth()),
                   static_cast<float>(backgroundImage->getHeight())),
        viewport,
        imagePaint);
}
```

Run this setup at init, resource recovery, or resize time — not every frame. For OpenGL / OpenGL ES backends, make sure the target Canvas's graphics context is initialized and usable before calling `loadImageFromRGBA`.

When many objects share only a few appearances, merge them into an Atlas:

```cpp
wsc::Paint imagePaint;
imagePaint.setColor(wsc::Color::WHITE);

for (const Item& item : visibleItems) {
    const wsc::RectF src = atlasCell(item.type, item.state);
    canvas.drawImage(atlas, src, item.bounds, imagePaint);
}
```

Objects in the same atlas share one texture, which reduces state changes and repeat resource creation. Size the atlas based on actual display sizes; RGBA texture cost is roughly `width × height × 4` bytes, and oversized atlases cost more upload time and memory.

### Control Cache Invalidation Scope

Split caches along the smallest independently changing UI unit. For example, when only two groups in a list change, rebuild the Picture for just those groups instead of invalidating the whole list.

```cpp
void moveItem(int sourceGroup, int destinationGroup) {
    updateModel(sourceGroup, destinationGroup);
    invalidateGroup(sourceGroup);
    invalidateGroup(destinationGroup);
}
```

Invalidation should match what the cache actually depends on. If the pointer position only affects a dynamic overlay, it should not invalidate the stable Picture. Animating objects can be temporarily excluded from the stable cache and re-integrated when the animation ends:

```cpp
drawStableComponents(canvas);
for (const Motion& motion : motions) {
    drawMotionSprite(canvas, motion);
}
```

For batch animations, stagger their start or end so cache rebuilds spread across frames instead of clustering on one.

### Memory vs. Performance

Rasterized Pictures, Images, and Atlases all consume texture memory. When you set a cache budget, also account for CPU-side pixel copies, mipmaps, MSAA buffers, and driver overhead.

When a cache hits only a handful of frames or is evicted often, rasterization cost may exceed its benefit. Shrink the cache scope, or fall back to plain `drawPicture`. When a Surface or graphics context is destroyed, release the related Pictures, Images, and Atlases, and rebuild them on demand after the context is restored.

### Validate Interactive Performance

Interactive performance tests should cover the full gesture: press, continuous movement, release, and animation end. Besides average FPS, record the max frame time, the count of frames over the target budget, and the time and memory peak of the first cache build.

Animation duration must also respect the refresh rate. At 60 FPS, 50 ms is only ~3 frames; motions that need to be legible usually need longer durations and benefit from staggering to lower per-frame load.

A full worked example lives in the [Android interactive Canvas performance guide](../performance/ANDROID_INTERACTIVE_PERFORMANCE.md).

---

## 11.12 Optimization Checklist

| Optimization | Scenario | Effect |
|--------------|----------|--------|
| `drawPictureRasterized` | Complex static UI parts | Avoid per-frame recompute; direct texture blit |
| `quickReject` | Long lists, large canvases | Skip invisible elements |
| Reuse Paint | Many similar elements | Reduce object churn and state changes |
| Mipmaps + LINEAR sampling | Downscaled images | Reduce aliasing; GPU-friendly |
| Partial `updateImageRGBA` | Dynamic textures | Avoid full re-upload |
| Async pixel readback | Screenshots, recording | Do not stall the pipeline |
| Dirty region + clipRect | Partial UI updates | Reduce drawn area |
| Conditional frame updates | Static scenes | Save power, lower GPU load |
| Batch same-paint draws | List rendering | Reduce shader changes |
| Component-level cache invalidation | Drag, board, editor | Avoid whole-screen rebuild |
| Dynamic overlay layer | Drag and batch animation | Decouple stable cache from per-frame objects |
| Image Atlas | Many repeated complex sprites | Fewer textures, fewer state changes |
| Staggered refreshes | Batch animations | Spread cache rebuild peaks |
| Cache budget + eviction stats | Memory-constrained devices | Balance hit rate against resident memory |

---

## 11.13 Summary

This chapter covered:

- [x] Picture recording and replay
- [x] Rasterized cache (drawPictureRasterized)
- [x] Skipping invisible content with quickReject
- [x] Tricks to reduce state changes
- [x] Image resource optimization (mipmaps, reuse, partial updates)
- [x] Render stats and GPU timing
- [x] Async pixel readback
- [x] Dirty region tracking and conditional frame updates
- [x] Benchmarking methodology
- [x] Splitting stable and dynamic layers
- [x] Image Atlas and cache invalidation boundaries
- [x] Component-level dirty flags and animation overlays
- [x] Cache budget, lifetime, and platform differences
- [x] Frame budget and perceptual validation for interactive animation

**Next chapter**: [Cross-Platform in Practice](./12-cross-platform.md) — integrate WhatsCanvas on Android, iOS, and the Web.
