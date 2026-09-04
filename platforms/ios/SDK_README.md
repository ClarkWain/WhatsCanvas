# WhatsCanvas iOS SDK

`WhatsCanvas.xcframework` contains the static Metal/CoreText library and public
C++ headers for iOS 15 or newer. It includes an `arm64` device slice and a
universal `arm64`/`x86_64` simulator slice.

Add the XCFramework to the application target, include `<wsc/wsc.h>` from
C++/Objective-C++ code, and link these Apple frameworks:

- Metal
- Foundation
- QuartzCore
- CoreGraphics
- CoreText
- UIKit

The application owns its `MTLDevice`, presentation surface, lifecycle, and code
signing. See `platforms/ios/README.md` and `doc/public/platforms/IOS_BUILD_NOTES.md` in the source
repository for the complete integration contract.
