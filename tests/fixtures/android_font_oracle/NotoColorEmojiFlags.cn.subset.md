# Noto Color Emoji Flags CN subset

`NotoColorEmojiFlags.cn.subset.ttf` is a deterministic test-only subset of
`/system/fonts/NotoColorEmojiFlags.ttf` from the Android 13 / API 33 emulator
image with build fingerprint:

`google/sdk_gphone64_x86_64/emu64x:13/TE1A.240213.009/12342917:userdebug/dev-keys`

- Original SHA-256: `A20C15A60B7761B8241EE5FE2B7538ACCA06198B9C5E2333CE87912F32183DD2`
- Subset SHA-256: `F38055D247C711F433E4C4A6469246A655294A67B9D12141936705AC7BCF90D1`
- Retained Unicode scalars: U+1F1E8, U+1F1F3
- Retained behavior: GSUB `ccmp` ligature and the corresponding CBDT/CBLC glyph
- Generated with `fontTools pyftsubset`, retaining all layout features, names,
  glyph names, recommended glyphs, and legacy/symbol cmaps; hints were removed.

The embedded name table preserves `Copyright 2013 Google Inc.` and the SIL Open
Font License 1.1 notice. The complete OFL 1.1 text is also present in
`third_party/harfbuzz/test/COPYING`.
