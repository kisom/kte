# kge Configuration

kge loads configuration from `~/.config/kte/kge.toml`. If no TOML file is
found, it falls back to the legacy `kge.ini` format.

## TOML Format

```toml
[window]
fullscreen = false
columns = 80
rows = 42

[font]
# Default font and size
name = "default"
size = 18.0
# Font used in code mode (monospace)
code = "default"
# Font used in writing mode (proportional)
writing = "crimsonpro"

[appearance]
theme = "nord"
# "dark" or "light" for themes with variants
background = "dark"

[editor]
syntax = true
```

## Sections

### `[window]`

| Key          | Type | Default | Description                     |
|--------------|------|---------|---------------------------------|
| `fullscreen` | bool | false   | Start in fullscreen mode        |
| `columns`    | int  | 80      | Initial window width in columns |
| `rows`       | int  | 42      | Initial window height in rows   |

### `[font]`

| Key       | Type   | Default      | Description                              |
|-----------|--------|--------------|------------------------------------------|
| `name`    | string | "default"    | Default font loaded at startup           |
| `size`    | float  | 18.0         | Font size in pixels                      |
| `code`    | string | "default"    | Font for code mode (monospace)           |
| `writing` | string | "crimsonpro" | Font for writing mode (proportional)     |

### `[appearance]`

| Key          | Type   | Default | Description                             |
|--------------|--------|---------|-----------------------------------------|
| `theme`      | string | "nord"  | Color theme                             |
| `background` | string | "dark"  | Background mode: "dark" or "light"      |

### `[editor]`

| Key      | Type | Default | Description                  |
|----------|------|---------|------------------------------|
| `syntax` | bool | true    | Enable syntax highlighting   |

## Edit Modes

kge has two edit modes that control which font is used:

- **code** — Uses the monospace font (`font.code`). Default for source files.
- **writing** — Uses the proportional font (`font.writing`). Auto-detected
  for `.txt`, `.md`, `.markdown`, `.rst`, `.org`, `.tex`, `.adoc`, and
  `.asciidoc` files.

Toggle with `C-k m` or `: mode [code|writing]`.

## Available Fonts

### Monospace

b612, berkeley, berkeley-bold, brassmono, brassmono-bold, brassmonocode,
brassmonocode-bold, fira, go, ibm, idealist, inconsolata, inconsolataex,
iosevka, iosevkaex, sharetech, space, syne, triplicate, unispace

### Proportional (Serif)

crimsonpro, etbook, spectral

## Available Themes

amber, eink, everforest, gruvbox, kanagawa-paper, lcars, leuchtturm, nord,
old-book, orbital, plan9, solarized, tufte, weyland-yutani, zenburn

Themes with light/dark variants: eink, gruvbox, leuchtturm, old-book,
solarized. Set `background = "light"` or use `: background light`.

## Migrating from kge.ini

If you have an existing `kge.ini`, kge will still read it but prints a
notice to stderr suggesting migration. To migrate, create `kge.toml` in the
same directory (`~/.config/kte/`) using the format above. The TOML file
takes priority when both exist.

The INI keys map to TOML as follows:

| INI key       | TOML equivalent          |
|---------------|--------------------------|
| `fullscreen`  | `window.fullscreen`      |
| `columns`     | `window.columns`         |
| `rows`        | `window.rows`            |
| `font`        | `font.name`              |
| `font_size`   | `font.size`              |
| `theme`       | `appearance.theme`       |
| `background`  | `appearance.background`  |
| `syntax`      | `editor.syntax`          |

New keys `font.code` and `font.writing` have no INI equivalent (the INI
parser accepts `code_font` and `writing_font` if needed).
