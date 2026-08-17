# Android font configuration corpus

This corpus has two layers:

1. `api*.xml` and `vendor_product.xml` are small, sanitized compatibility
   fixtures. Their API labels identify a schema/feature era; they are not
   byte-for-byte copies of a device image.
2. `*_complete.xml` files are complete configuration captures pulled from the
   stated Android image/device. They contain XML configuration metadata only;
   no system or OEM font binary is redistributed.

Complete capture provenance:

| Fixture | Partition/source | Build fingerprint | SHA-256 |
| --- | --- | --- | --- |
| `aosp_api33_complete.xml` | `/system/etc/fonts.xml`, Google API 33 `sdk_gphone64_x86_64` AVD | `google/sdk_gphone64_x86_64/emu64x:13/TE1A.240213.009/12342917:userdebug/dev-keys` | `e02292abaa08c6e4661841eed58869591a74cc8f5c1bb0ebc1c31de5634681eb` |
| `xiaomi_miui12_api30_complete.xml` | `/system/etc/fonts.xml`, Redmi K30 (`phoenix`), MIUI 12.5.6 / Android 11 | `Redmi/phoenix/phoenix:11/RKQ1.200826.002/V12.5.6.0.RGHCNXM:user/release-keys` | `63de5e95304d9ee0b2dc2ea53d73240306aedf30d626804542e24c3393b9183a` |

The device images exposed no additional `*font*.xml` file at the root of
`/product/etc`, `/vendor/etc`, `/system/product/etc`, `/system/vendor/etc`, or
`/odm/etc` when captured on 2026-08-17. The system file is therefore the full
configuration input visible on each image, rather than a hand-picked excerpt.

The corpus currently covers:

- API 21 legacy `nameset`/`fileset`, grouped `family-list` names, and
  case-insensitive alias targets;
- API 23 named families and exact-weight aliases;
- API 28 target-specific fallback and variation-axis validation;
- API 29 locale/script parent ranking;
- API 33 emoji versus text presentation;
- API 35 TTC/variable-font face identity;
- product/vendor additions, empty font records, incomplete aliases, and an
  alias whose target family is absent.
- a full AOSP API 33 variable-Roboto/Noto fallback graph (367 parsed faces,
  24 aliases);
- a full MIUI API 30 graph (360 parsed faces, 24 aliases), including the OEM
  `mipro`/MiLan variable-font instances.

`WhatsCanvasAndroidFontConfigTests` loads every file from disk, asserts its
stable record counts, and checks the feature-specific matching behavior.
When the fixed-revision Skia producer is configured, the separate corpus oracle
also classifies these fixtures as strict equality or reviewed WhatsCanvas
extensions; see `../android_font_oracle/corpus_manifest.json`.
Malformed whole-document rejection remains a separate inline test because an
invalid XML document cannot safely participate in merged-config fixtures.

Future additions must state source Android release, device/build fingerprint,
partition, capture date, and checksum. Do not add proprietary font binaries;
if redistribution of a configuration capture is uncertain, add a sanitized
structural fixture and document that it is not a complete capture.
