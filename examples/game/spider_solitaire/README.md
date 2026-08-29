# Spider Solitaire

A complete, image-free Spider Solitaire game rendered entirely with WhatsCanvas.

```sh
./build.sh
```

Mouse controls: click or drag a same-suit descending run onto an empty column or a card one rank higher. Click the stock to deal another row. Empty columns must be filled before dealing.

Keyboard shortcuts: `N` new deal, `U` undo, `H` hint, `1`/`2`/`4` difficulty, `Esc` quit.

Run the deterministic rules suite without opening a window:

```sh
./build/SpiderSolitaire --self-test
```
