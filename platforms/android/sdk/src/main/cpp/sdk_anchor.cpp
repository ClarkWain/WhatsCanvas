// The Android SDK exposes WhatsCanvas through a shared Prefab module. The
// actual public API lives in the linked WhatsCanvasOpenGLES static archive;
// this anchor gives CMake/AGP a concrete shared-library target to publish.
extern "C" int whatscanvas_android_sdk_anchor()
{
    return 0;
}
