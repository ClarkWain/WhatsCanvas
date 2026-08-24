#include <wsc/wsc.h>

int main() {
    auto canvas = wsc::Canvas::create(wsc::Canvas::Backend::Metal, 1, 1);
    return canvas ? WSC_VERSION_MAJOR : 0;
}
