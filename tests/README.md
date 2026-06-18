# WhatsCanvas Tests

This directory is the top-level home for WhatsCanvas validation beyond ad-hoc local commands.

## Current Entry Points

- `ctest -C Debug -R ^WhatsCanvasGraphicsStateStackTests$ --output-on-failure`: lightweight unit executable covering `GraphicsStateStack` save/restore semantics and header-only `Path` behavior such as even-odd contains, stroke hit-testing, trim, and reverse.
- `ctest -C Debug -L smoke --output-on-failure`: standard entry for the registered smoke/example script gates.
- `scripts/smoke_test.bat` / `scripts/smoke_test.sh`: fixed-time first-frame smoke gate.
- `scripts/clip_path_smoke.bat` / `scripts/clip_path_smoke.sh`: stacked non-rect `clipPath` smoke gate.
- `scripts/examples_smoke.bat` / `scripts/examples_smoke.sh`: independent example build gate.
- `scripts/regression_smoke.bat` / `scripts/regression_smoke.sh`: strict local pixel-baseline gate.

## Intended Growth

- expand unit coverage beyond the current state-stack and basic path semantics tests into clip/query helpers and text measurement helpers;
- render-scene fixtures that can be driven by `ctest` without recursively rebuilding the whole tree;
- backend-consistency tests once alternate backends land;
- stress tests for resize, saveLayer, clip nesting, and resource lifetime.