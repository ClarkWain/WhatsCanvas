# Spider Solitaire

A polished, fully playable Spider Solitaire game rendered entirely with WhatsCanvas.
It includes 1/2/4-suit difficulty, undo and hints, responsive high-DPI input, and animated dealing and completed-run collection.

![Spider Solitaire gameplay](screenshot.png)

```sh
./build.sh
```

Build scripts default to `Release` for smooth gameplay. Pass `--debug` when a debug build is needed.

Mouse controls: click or drag a same-suit descending run onto an empty column or a card one rank higher. Click the stock to deal another row. Empty columns must be filled before dealing.

Keyboard shortcuts: `N` new deal, `U` undo, `H` hint, `1`/`2`/`4` difficulty, `Esc` quit.

Run the deterministic rules suite without opening a window:

```sh
./build/SpiderSolitaire --self-test
```

The Android port includes deterministic drag profiling and a native Android
Canvas control renderer. See the
[interactive performance case study](../../../doc/ANDROID_INTERACTIVE_PERFORMANCE.md)
for the 1 FPS regression, shared card-atlas design, animation review, and final
device measurements.
