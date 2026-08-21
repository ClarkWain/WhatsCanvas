# Noto Color Emoji demo subset

- Upstream: <https://github.com/googlefonts/noto-emoji>
- Upstream release: `v2.051` (2025)
- Source asset: `fonts/NotoColorEmoji.ttf`
- Subsetting tool: fonttools `4.63.0`
- Output SHA-256:
  `aafd1612b73c48ed1aa2ab59b03d9c6b945a136d0b84ccdb469e9ef01462c289`

The subset contains only the canonical scene text `👩🏽‍💻🇨🇳8️⃣`. It keeps all
layout features plus the CBDT/CBLC color bitmap tables required by the portable
FreeType glyph-atlas path.

Regenerate from the pinned release:

```sh
curl -L --fail \
  -o NotoColorEmoji.ttf \
  https://raw.githubusercontent.com/googlefonts/noto-emoji/v2.051/fonts/NotoColorEmoji.ttf
pyftsubset NotoColorEmoji.ttf \
  --output-file=NotoColorEmoji.demo.subset.ttf \
  --text='👩🏽‍💻🇨🇳8️⃣' \
  --layout-features='*' \
  --glyph-names \
  --symbol-cmap \
  --legacy-cmap \
  --notdef-glyph \
  --notdef-outline \
  --recommended-glyphs
```

The SIL Open Font License is included beside the subset.
