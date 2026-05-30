# PrismCanvas Tests

This directory is the top-level home for PrismCanvas validation beyond ad-hoc local commands.

## Current Entry Points

- `ctest -C Debug -R ^PrismCanvasGraphicsStateStackTests$ --output-on-failure`: lightweight unit executable covering `GraphicsStateStack` save/restore semantics and header-only `Path` behavior such as even-odd contains, stroke hit-testing, trim, and reverse.
- `ctest -C Debug -L smoke --output-on-failure`: standard entry for the registered smoke/example script gates.
- `smoke_test.bat` / `smoke_test.sh`: fixed-time first-frame smoke gate.
- `clip_path_smoke.bat` / `clip_path_smoke.sh`: stacked non-rect `clipPath` smoke gate.
- `examples_smoke.bat` / `examples_smoke.sh`: independent example build gate.
- `regression_smoke.bat` / `regression_smoke.sh`: strict local pixel-baseline gate.

## Intended Growth

- expand unit coverage beyond the current state-stack and basic path semantics tests into clip/query helpers and text measurement helpers;
- render-scene fixtures that can be driven by `ctest` without recursively rebuilding the whole tree;
- backend-consistency tests once alternate backends land;
- stress tests for resize, saveLayer, clip nesting, and resource lifetime.