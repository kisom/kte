Themes in kte
=============

Overview
--------

kte's GUI frontend (kge) uses ImGui for rendering and supports multiple
color themes. Themes define the visual appearance of the editor
interface including colors for text, backgrounds, buttons, borders, and
other UI elements.

Theme files are located in the `themes/` directory and are header-only
C++ files that configure ImGui's style system.

Available themes
----------------

Current themes (alphabetically):

- **amber** — Monochrome amber/black CRT-inspired theme
- **eink** — E-ink inspired high-contrast theme (light/dark variants)
- **everforest** — Warm, forest-inspired palette
- **gruvbox** — Retro groove color scheme (light/dark variants)
- **kanagawa-paper** — Inspired by traditional Japanese art
- **lcars** — Star Trek LCARS interface style
- **nord** — Arctic, north-bluish color palette
- **old-book** — Sepia-toned vintage book aesthetic (light/dark
  variants)
- **orbital** — Space-themed dark palette
- **plan9** — Minimalist Plan 9 from Bell Labs inspired
- **solarized** — Ethan Schoonover's Solarized (light/dark variants)
- **weyland-yutani** — Alien franchise corporate aesthetic
- **zenburn** — Low-contrast, easy-on-the-eyes theme

Configuration
-------------

Themes are configured via `$HOME/.config/kte/kge.ini`:

```ini
theme = nord
background = dark
```

- `theme` — The theme name (e.g., "nord", "gruvbox", "solarized")
- `background` — Either "dark" or "light" (for themes supporting both
  variants)

Themes can also be switched at runtime using the `:theme <name>`
command.

Theme structure
---------------

Each theme is a header file in `themes/` that defines one or more
functions to apply the theme. The basic structure:

1. **Include ThemeHelpers.h** — Provides the `RGBA()` helper function
2. **Define palette** — Create `ImVec4` color constants using
   `RGBA(0xRRGGBB)`
3. **Get ImGui style** — Obtain reference via `ImGui::GetStyle()`
4. **Set style parameters** — Configure padding, rounding, border sizes,
   etc.
5. **Assign colors** — Map palette to `ImGuiCol_*` enum values

### Minimal example structure

```cpp
// themes/MyTheme.h
#pragma once
#include "ThemeHelpers.h"

static void
ApplyMyTheme()
{
    // 1. Define color palette
    const ImVec4 bg     = RGBA(0x1e1e1e);
    const ImVec4 fg     = RGBA(0xd4d4d4);
    const ImVec4 accent = RGBA(0x007acc);
    
    // 2. Get style reference
    ImGuiStyle &style = ImGui::GetStyle();
    
    // 3. Set style parameters
    style.WindowPadding    = ImVec2(8.0f, 8.0f);
    style.FrameRounding    = 3.0f;
    style.WindowBorderSize = 1.0f;
    // ... additional style parameters
    
    // 4. Assign colors
    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_Text]     = fg;
    colors[ImGuiCol_WindowBg] = bg;
    colors[ImGuiCol_Button]   = accent;
    // ... additional color assignments
}
```

The RGBA() helper
-----------------

The `RGBA()` function (defined in `themes/ThemeHelpers.h`) converts
packed RGB hex values to ImGui's `ImVec4` format:

```cpp
const ImVec4 color = RGBA(0xRRGGBB);        // Opaque (alpha = 1.0)
const ImVec4 color = RGBA(0xRRGGBB, 0.5f);  // With custom alpha
```

Examples:

```cpp
const ImVec4 white = RGBA(0xFFFFFF);
const ImVec4 black = RGBA(0x000000);
const ImVec4 red   = RGBA(0xFF0000);
const ImVec4 blue  = RGBA(0x0000FF);
const ImVec4 semi  = RGBA(0x808080, 0.5f);  // 50% transparent gray
```

ImGui color elements
--------------------

Themes must define colors for ImGui's UI elements. Key `ImGuiCol_*`
values:

### Text

- `ImGuiCol_Text` — Main text color
- `ImGuiCol_TextDisabled` — Disabled/grayed-out text
- `ImGuiCol_TextSelectedBg` — Text selection background

### Windows and backgrounds

- `ImGuiCol_WindowBg` — Window background
- `ImGuiCol_ChildBg` — Child window background
- `ImGuiCol_PopupBg` — Popup window background

### Borders

- `ImGuiCol_Border` — Border color
- `ImGuiCol_BorderShadow` — Border shadow (often transparent)

### Frames (input fields, etc.)

- `ImGuiCol_FrameBg` — Frame background (normal state)
- `ImGuiCol_FrameBgHovered` — Frame background when hovered
- `ImGuiCol_FrameBgActive` — Frame background when active/clicked

### Title bars

- `ImGuiCol_TitleBg` — Title bar (unfocused)
- `ImGuiCol_TitleBgActive` — Title bar (focused)
- `ImGuiCol_TitleBgCollapsed` — Collapsed title bar

### Interactive elements

- `ImGuiCol_Button` — Button background
- `ImGuiCol_ButtonHovered` — Button when hovered
- `ImGuiCol_ButtonActive` — Button when pressed
- `ImGuiCol_CheckMark` — Checkmark/radio button indicator
- `ImGuiCol_SliderGrab` — Slider grab handle
- `ImGuiCol_SliderGrabActive` — Slider grab when dragging

### Headers and separators

- `ImGuiCol_Header` — Header (tree nodes, collapsing headers)
- `ImGuiCol_HeaderHovered` — Header when hovered
- `ImGuiCol_HeaderActive` — Header when clicked
- `ImGuiCol_Separator` — Separator line
- `ImGuiCol_SeparatorHovered` — Separator when hovered
- `ImGuiCol_SeparatorActive` — Separator when dragged

### Scrollbars

- `ImGuiCol_ScrollbarBg` — Scrollbar background
- `ImGuiCol_ScrollbarGrab` — Scrollbar grab
- `ImGuiCol_ScrollbarGrabHovered` — Scrollbar grab when hovered
- `ImGuiCol_ScrollbarGrabActive` — Scrollbar grab when dragging

### Tabs

- `ImGuiCol_Tab` — Tab (inactive)
- `ImGuiCol_TabHovered` — Tab when hovered
- `ImGuiCol_TabActive` — Tab (active)
- `ImGuiCol_TabUnfocused` — Tab in unfocused window
- `ImGuiCol_TabUnfocusedActive` — Active tab in unfocused window

### Tables

- `ImGuiCol_TableHeaderBg` — Table header background
- `ImGuiCol_TableBorderStrong` — Strong table borders
- `ImGuiCol_TableBorderLight` — Light table borders
- `ImGuiCol_TableRowBg` — Table row background
- `ImGuiCol_TableRowBgAlt` — Alternating table row background

### Navigation and overlays

- `ImGuiCol_MenuBarBg` — Menu bar background
- `ImGuiCol_ResizeGrip` — Resize grip indicator
- `ImGuiCol_ResizeGripHovered` — Resize grip when hovered
- `ImGuiCol_ResizeGripActive` — Resize grip when dragging
- `ImGuiCol_DragDropTarget` — Drag-and-drop target highlight
- `ImGuiCol_NavHighlight` — Navigation highlight
- `ImGuiCol_NavWindowingHighlight` — Window navigation highlight
- `ImGuiCol_NavWindowingDimBg` — Window navigation dim background
- `ImGuiCol_ModalWindowDimBg` — Modal window dim background

### Plots (graphs)

- `ImGuiCol_PlotLines` — Plot line color
- `ImGuiCol_PlotLinesHovered` — Plot line when hovered
- `ImGuiCol_PlotHistogram` — Histogram color
- `ImGuiCol_PlotHistogramHovered` — Histogram when hovered

Style parameters
----------------

In addition to colors, themes can customize style parameters:

```cpp
ImGuiStyle &style = ImGui::GetStyle();

// Padding and spacing
style.WindowPadding    = ImVec2(8.0f, 8.0f);   // Window content padding
style.FramePadding     = ImVec2(6.0f, 4.0f);   // Frame (input fields) padding
style.CellPadding      = ImVec2(6.0f, 4.0f);   // Table cell padding
style.ItemSpacing      = ImVec2(6.0f, 6.0f);   // Space between items
style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);   // Space within composite items

// Rounding
style.WindowRounding = 4.0f;  // Window corner rounding
style.FrameRounding  = 3.0f;  // Frame corner rounding
style.PopupRounding  = 4.0f;  // Popup corner rounding
style.GrabRounding   = 3.0f;  // Grab handle rounding
style.TabRounding    = 4.0f;  // Tab corner rounding

// Borders
style.WindowBorderSize = 1.0f;  // Window border width
style.FrameBorderSize  = 1.0f;  // Frame border width

// Scrollbars
style.ScrollbarSize = 14.0f;   // Scrollbar width
style.GrabMinSize   = 10.0f;   // Minimum grab handle size
```

Creating a new theme
--------------------

Follow these steps to add a new theme to kte:

### 1. Create the theme file

Create a new header file in `themes/` (e.g., `themes/MyTheme.h`):

```cpp
// themes/MyTheme.h — Brief description
#pragma once
#include "ThemeHelpers.h"

// Expects to be included from GUITheme.h after <imgui.h> and RGBA() helper

static void
ApplyMyTheme()
{
    // Define your color palette
    const ImVec4 background = RGBA(0x1e1e1e);
    const ImVec4 foreground = RGBA(0xd4d4d4);
    const ImVec4 accent     = RGBA(0x007acc);
    // ... more colors
    
    ImGuiStyle &style = ImGui::GetStyle();
    
    // Configure style parameters
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    // ... more style settings
    
    ImVec4 *colors = style.Colors;
    
    // Assign all required colors
    colors[ImGuiCol_Text]     = foreground;
    colors[ImGuiCol_WindowBg] = background;
    // ... assign all other ImGuiCol_* values
}
```

Refer to existing themes like `Nord.h` for a complete example of all
required color assignments.

### 2. Add theme to GUITheme.h

Edit `GUITheme.h` to integrate your theme:

**a) Add to ThemeId enum:**

```cpp
enum class ThemeId {
    // ... existing themes
    MyTheme = 13,  // Use next available number
};
```

**b) Include your theme header:**

```cpp
// After other theme includes
#include "themes/MyTheme.h"
```

**c) Create wrapper class in detail namespace:**

```cpp
namespace detail {
// ... existing theme classes

struct MyThemeWrapper final : Theme {
    [[nodiscard]] const char *Name() const override
    {
        return "mytheme";  // Lowercase canonical name
    }
    
    void Apply() const override
    {
        ApplyMyTheme();
    }
    
    ThemeId Id() override
    {
        return ThemeId::MyTheme;
    }
};
} // namespace detail
```

**d) Register in ThemeRegistry():**

```cpp
static const std::vector<std::unique_ptr<Theme>> &
ThemeRegistry()
{
    static std::vector<std::unique_ptr<Theme>> reg;
    if (reg.empty()) {
        // Add in alphabetical order by canonical name
        reg.emplace_back(std::make_unique<detail::AmberTheme>());
        // ... existing themes
        reg.emplace_back(std::make_unique<detail::MyThemeWrapper>());
        // ... remaining themes
    }
    return reg;
}
```

### 3. Test your theme

Rebuild kte and test:

```bash
# Set theme in config
echo "theme = mytheme" >> ~/.config/kte/kge.ini

# Or switch at runtime
kge
:theme mytheme
```

Light/Dark theme variants
--------------------------

Some themes support both light and dark background modes. To implement
this:

### 1. Create separate functions for each variant

```cpp
// themes/MyTheme.h
#pragma once
#include "ThemeHelpers.h"

static void
ApplyMyThemeDark()
{
    const ImVec4 bg = RGBA(0x1e1e1e);  // Dark background
    const ImVec4 fg = RGBA(0xd4d4d4);  // Light text
    // ... rest of dark theme
}

static void
ApplyMyThemeLight()
{
    const ImVec4 bg = RGBA(0xffffff);  // Light background
    const ImVec4 fg = RGBA(0x1e1e1e);  // Dark text
    // ... rest of light theme
}
```

### 2. Check background mode in Apply()

```cpp
// In GUITheme.h wrapper class
struct MyThemeWrapper final : Theme {
    // ... Name() and Id() methods
    
    void Apply() const override
    {
        if (gBackgroundMode == BackgroundMode::Dark)
            ApplyMyThemeDark();
        else
            ApplyMyThemeLight();
    }
};
```

See `Solarized.h`, `Gruvbox.h`, `EInk.h`, or `OldBook.h` for complete
examples.

Updating existing themes
------------------------

To modify an existing theme:

### 1. Locate the theme file

Theme files are in `themes/` directory. For example, Nord theme is in
`themes/Nord.h`.

### 2. Modify colors or style

Edit the `ApplyXxxTheme()` function:

- Update palette color definitions
- Change style parameters
- Reassign `ImGuiCol_*` values

### 3. Rebuild and test

```bash
# Rebuild kte
cmake --build build

# Test changes
./build/kge
```

Changes take effect immediately on next launch or theme switch.

Best practices
--------------

When creating or updating themes:

1. **Start from an existing theme** — Copy a similar theme as a
   template (e.g., `Nord.h` for dark themes, `Solarized.h` for
   light/dark variants)

2. **Define a complete palette first** — Create all color constants at
   the top before assigning them

3. **Assign all colors** — Ensure every `ImGuiCol_*` value is set to
   avoid inheriting unexpected colors

4. **Use consistent naming** — Follow existing conventions (e.g.,
   `nord0`, `base03`, descriptive names)

5. **Test interactivity** — Verify hover, active, and disabled states
   for buttons, frames, and other interactive elements

6. **Consider contrast** — Ensure text is readable against backgrounds;
   test with different content

7. **Test transparency** — Use alpha values carefully for overlays, dim
   backgrounds, and selection highlights

8. **Match style to theme** — Adjust rounding, padding, and borders to
   suit the theme's aesthetic (e.g., sharp corners for retro themes,
   rounded for modern)

9. **Document inspiration** — Note the color scheme's origin or
   inspiration in the file header

10. **Maintain alphabetical order** — When registering in
    `ThemeRegistry()`, maintain alphabetical order by canonical name

Troubleshooting
---------------

### Theme not appearing

- Check that the theme is registered in `ThemeRegistry()` in
  alphabetical order
- Verify the canonical name matches what you're using in config or
  commands
- Ensure the theme header is included in `GUITheme.h`

### Colors look wrong

- Verify hex color values are in 0xRRGGBB format (not 0xBBGGRR)
- Check alpha values for semi-transparent elements
- Ensure all `ImGuiCol_*` values are assigned

### Style inconsistent

- Make sure style parameters are set before color assignments
- Check that you're getting `ImGui::GetStyle()` reference correctly
- Verify no global style changes are overriding theme settings

References
----------

- ImGui style
  reference: https://github.com/ocornut/imgui/blob/master/imgui.h
- Existing themes in `themes/` directory
- Color palette resources: coolors.co, colorhunt.co
