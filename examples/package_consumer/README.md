# WhatsCanvas Package Consumer Example

This stand-alone example shows how an external project consumes an installed
WhatsCanvas package with `find_package(WhatsCanvas CONFIG REQUIRED)`.

It demonstrates two different decisions:

1. Link-time package target selection
2. Runtime `Canvas::Backend` selection

## Configure Against A Package

Point `CMAKE_PREFIX_PATH` at an installed/staged package, for example the local
package generated under `out/package/Release`.

### Software Consumer

```powershell
cmake -S examples/package_consumer -B build-example-consumer \
  -DCMAKE_PREFIX_PATH="I:/WhatsCanvas/out/package/Release" \
  -DWHATSCANVAS_PACKAGE_TARGET=Software \
  -DWHATSCANVAS_RUNTIME_BACKEND=Software

cmake --build build-example-consumer --config Release
```

### Vulkan Consumer

Vulkan is packaged inside `WhatsCanvas::OpenGL`, so you still link the OpenGL
package target and select `Backend::Vulkan` at runtime.

```powershell
cmake -S examples/package_consumer -B build-example-consumer-vk \
  -DCMAKE_PREFIX_PATH="I:/WhatsCanvas/out/package/Release" \
  -DWHATSCANVAS_PACKAGE_TARGET=OpenGL \
  -DWHATSCANVAS_RUNTIME_BACKEND=Vulkan

cmake --build build-example-consumer-vk --config Release
./build-example-consumer-vk/Release/WhatsCanvasPackageConsumerExample
```

### OpenGLES Consumer

```powershell
cmake -S examples/package_consumer -B build-example-consumer-gles \
  -DCMAKE_PREFIX_PATH="I:/WhatsCanvas/out/package/Release" \
  -DWHATSCANVAS_PACKAGE_TARGET=OpenGLES \
  -DWHATSCANVAS_RUNTIME_BACKEND=OpenGLES

cmake --build build-example-consumer-gles --config Release
```

## Notes

- `WhatsCanvas::OpenGL` gives you the desktop GPU renderer DLL. With the
  package built using `--vulkan`, the same DLL also contains the Vulkan
  backend implementation.
- `WhatsCanvas::OpenGLES` is a separate package target and DLL.
- `WhatsCanvas::Software` is the dependency-free CPU renderer.
- For the runnable off-screen cases in this example (`Software` and `Vulkan`),
  the executable writes `package_consumer_output.ppm` next to the built exe.
- This example does not create an OpenGL/OpenGLES window/context for you.
  For those runtime backends, create the context in your app and call
  `wsc::Canvas::loadOpenGL(...)` before `Canvas::create(...)`.