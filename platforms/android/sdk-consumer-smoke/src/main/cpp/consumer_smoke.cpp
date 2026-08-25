#include <wsc/wsc.h>

extern "C" int whatscanvasAndroidConsumerSmoke() {
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::OpenGLES, 1, 1);
    return canvas ? WSC_VERSION_MAJOR : -1;
}
