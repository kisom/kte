# kge Configuration

kge reads `~/.config/kte/kge.toml` when it starts. Every setting is
optional; leave out anything you're happy with. If the file is missing,
or has a syntax error (kge prints the error), kge falls back to the
older `kge.ini` format described at the end.

The terminal editor, `kte`, has no configuration file.

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

`columns` and `rows` outside 1–1000 are ignored.

### `[font]`

| Key       | Type   | Default      | Description                              |
|-----------|--------|--------------|------------------------------------------|
| `name`    | string | "default"    | Base font; `code` and `writing` fall back to it |
| `size`    | float  | 18.0         | Font size in pixels (4–200)              |
| `code`    | string | same as `name` | Font for code mode                     |
| `writing` | string | "crimsonpro" | Font for writing mode                    |

How the three font names fit together:

- `code` follows `name` unless you set it.
- `writing` stays "crimsonpro" unless you set it, or you set `name` to
  something other than "default", in which case it follows `name`.

So `name = "iosevka"` on its own gives you Iosevka everywhere. Add
`writing = "etbook"` to keep a serif font for prose.

Each buffer is drawn in the font for its edit mode, so in practice
`code` and `writing` are the fonts you see; `name` only supplies their
defaults.

`"default"` is Berkeley Mono Bold (the same as `berkeley-bold`). Set
fonts here; the size can also change while kge runs, with
`: font-size NUM` (press `C-k ;` for the prompt) or Ctrl `+`/`-`/`0`
(Cmd on macOS). Ctrl+0 goes back to `size`.

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
- **writing** — Uses the proportional font (`font.writing`), with no
  syntax highlighting. Files ending in `.txt`, `.md`, `.markdown`,
  `.rst`, `.org`, `.tex`, `.adoc` or `.asciidoc` open in this mode.

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

Themes with light and dark versions: eink, gruvbox, kanagawa-paper,
leuchtturm, old-book, solarized, tufte. Set `background = "light"` or
use `: background light`. See [docs/themes.md](docs/themes.md) for
descriptions and how to add a theme.

## Migrating from kge.ini

If you have an existing `kge.ini` and no `kge.toml`, kge still reads it
but prints a notice to stderr suggesting migration. To migrate, create `kge.toml` in the
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

| `code_font`   | `font.code`              |
| `writing_font`| `font.writing`           |
