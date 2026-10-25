# NotePadLX icons

Line icons drawn for NotePadLX (24×24 grid, 1.75 px strokes, round caps), licensed like the rest of NotePadLX (GPLv3).
They are templates: `NplxIconEngine` (src/ui/NplxStyle.cpp) replaces the colour tokens for the current light/dark
palette and the icon state (normal, disabled):

| token | meaning |
|---|---|
| `@fg` | main stroke |
| `@muted` | secondary stroke (document tab icons) |
| `@accent` | accent (blue) |
| `@danger` | close / record / error (red) |
| `@warn` | unsaved / warning (amber) |
| `@bg` | window background (cut-outs) |

Toolbar names match upstream `toolBarIcons[]` (new, open, save...), so toolbarButtonsConf.xml names still apply.
