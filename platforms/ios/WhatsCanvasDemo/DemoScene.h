#pragma once

#include <memory>

namespace wsc {
class Canvas;
class Image;
class Picture;
}

namespace whatscanvas::demo {

void createCheckerImage(wsc::Canvas &canvas,
                        std::unique_ptr<wsc::Image> &image);
std::shared_ptr<const wsc::Picture> recordStaticScene(
    wsc::Canvas &canvas, float width, float height,
    float safeTop, float safeBottom, float safeLeft, float safeRight);
void drawDynamicScene(wsc::Canvas &canvas, const wsc::Image *checkerImage,
                      float width, float height, float safeTop,
                      float safeBottom, float safeLeft, float safeRight,
                      float elapsedSeconds);

} // namespace whatscanvas::demo
