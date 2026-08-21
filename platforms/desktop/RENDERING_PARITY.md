# Desktop rendering parity

## Whole-scene scaling mismatch

The first Retina correction mapped Canvas points to physical pixels correctly,
but it still laid the showcase out using the desktop window's logical size.
Cards therefore reflowed to the window while fixed-size typography, strokes,
radii and feature geometry retained their original point sizes. The result was
not a uniform enlargement and did not match Android at the same physical
resolution.

The first fix used logical sizes measured from an Android screenshot. That
removed the immediate mismatch but coupled the contract to one device's DPR
and system bars. The long-term primary reference is now device-neutral and
rotation-symmetric:

- Landscape: 800 x 400 units.
- Portrait: 400 x 800 units.

The measured 786 x 377 / 393 x 759 pair remains available under the
`legacy_android` standard only for comparing historical captures.

`SceneViewport` now aspect-fits the orientation-specific reference canvas into
the host, centers it horizontally, anchors it at the top and applies one shared
translate/scale transform. The retained static `Picture` and dynamic animation
overlay use the same transform. Any unmatched host area keeps the scene's dark
background instead of stretching individual elements.

## Validation record

- A 1080 x 540 macOS render matches the geometry of the 2160 x 1080 Android
  screenshot after accounting for Retina 2x backing scale.
- Landscape (1080 x 540) and portrait (520 x 780 and 400 x 800) were rendered
  through both OpenGL and Software backends.
- At 1080 x 540, OpenGL versus Software mean absolute channel delta was
  0.009997. At 520 x 780 it was 0.011954.
- The viewport unit test covers both orientation reference sizes, uniform
  scale, horizontal centering and top anchoring.
- A 300-frame OpenGL benchmark at 1080 x 540 averaged 1.916 ms per frame
  (522 FPS) with 300 retained-picture cache hits and no misses. This is above
  the 60 FPS budget on the validation Mac.
- Desktop host smoke, Software renderer and iOS Metal pixel-parity tests pass.

The older Android reference screenshot shows the image-sampling card before
the cross-platform repeat-tile correction. Current Android, iOS and desktop
code intentionally use the same repeat mode, so that card should be compared
against a newly captured Android frame.

macOS may emit a one-time `GLD_TEXTURE_INDEX_BUFFER` diagnostic while creating
the OpenGL context. It is a driver-layer warning; the rendered output and the
Software comparison remain valid, but it should stay visible in validation
logs rather than being suppressed.
