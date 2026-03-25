// GUITheme.h — theming helpers and background mode
#pragma once

#include <cstddef>
#include <string>
#include <algorithm>
#include <cctype>

#include "Highlight.h"

// Cross-frontend theme change request hook: declared here, defined in Command.cc
namespace kte {
extern bool gThemeChangePending;
extern std::string gThemeChangeRequest; // raw user-provided name
// Qt GUI: cross-frontend font change hooks and current font state
extern bool gFontChangePending;
extern std::string gFontFamilyRequest; // requested family (case-insensitive)
extern float gFontSizeRequest; // <= 0 means keep size
extern std::string gCurrentFontFamily; // last applied family (Qt)
extern float gCurrentFontSize; // last applied size (Qt)
// Qt GUI: request to show a visual font dialog (set by command handler)
extern bool gFontDialogRequested;
}

#if defined(KTE_USE_QT)
// Qt build: avoid hard dependency on ImGui headers/types.
// Provide a lightweight color vector matching ImVec4 fields used by renderers.
struct KteColor {
	float x{0}, y{0}, z{0}, w{1};
};

static inline KteColor
RGBA(unsigned int rgb, float a = 1.0f)
{
	const float r = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
	const float g = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
	const float b = static_cast<float>(rgb & 0xFF) / 255.0f;
	return {r, g, b, a};
}

namespace kte {
// Background mode selection for light/dark palettes
enum class BackgroundMode { Light, Dark };

// Global background mode; default to Dark to match prior defaults
static inline auto gBackgroundMode = BackgroundMode::Dark;


static inline void
SetBackgroundMode(const BackgroundMode m)
{
	gBackgroundMode = m;
}


static inline BackgroundMode
GetBackgroundMode()
{
	return gBackgroundMode;
}


// Minimal GUI palette for Qt builds. This mirrors the defaults used in the ImGui
// frontend (Nord-ish) and switches for light/dark background mode.
struct Palette {
	KteColor bg; // editor background
	KteColor fg; // default foreground text
	KteColor sel_bg; // selection background
	KteColor cur_bg; // cursor cell background
	KteColor status_bg; // status bar background
	KteColor status_fg; // status bar foreground
};

// Optional theme override (Qt): when set, GetPalette() will return this instead
// of the generic light/dark defaults. This allows honoring theme names in kge.ini.
static inline bool gPaletteOverride = false;
static inline Palette gOverridePalette{};
static inline std::string gOverrideThemeName = ""; // lowercased name

static inline Palette
GetPalette()
{
	const bool dark = (GetBackgroundMode() == BackgroundMode::Dark);
	if (gPaletteOverride) {
		return gOverridePalette;
	}
	if (dark) {
		return Palette{
			/*bg*/ RGBA(0x1C1C1E),
			/*fg*/ RGBA(0xDCDCDC),
			/*sel_bg*/ RGBA(0xC8C800, 0.35f),
			/*cur_bg*/ RGBA(0xC8C8FF, 0.50f),
			/*status_bg*/ RGBA(0x28282C),
			/*status_fg*/ RGBA(0xB4B48C)
		};
	} else {
		// Light palette tuned for readability
		return Palette{
			/*bg*/ RGBA(0xFBFBFC),
			/*fg*/ RGBA(0x30343A),
			/*sel_bg*/ RGBA(0x268BD2, 0.22f),
			/*cur_bg*/ RGBA(0x000000, 0.15f),
			/*status_bg*/ RGBA(0xE6E8EA),
			/*status_fg*/ RGBA(0x50555A)
		};
	}
}


// A few named palettes to provide visible differences between themes in Qt.
// These are approximate and palette-based (no widget style changes like ImGuiStyle).
static inline Palette
NordDark()
{
	return {
		/*bg*/RGBA(0x2E3440), /*fg*/RGBA(0xD8DEE9), /*sel_bg*/RGBA(0x88C0D0, 0.25f),
		/*cur_bg*/RGBA(0x81A1C1, 0.35f), /*status_bg*/RGBA(0x3B4252), /*status_fg*/RGBA(0xE5E9F0)
	};
}


static inline Palette
NordLight()
{
	return {
		/*bg*/RGBA(0xECEFF4), /*fg*/RGBA(0x2E3440), /*sel_bg*/RGBA(0x5E81AC, 0.22f),
		/*cur_bg*/RGBA(0x000000, 0.12f), /*status_bg*/RGBA(0xE5E9F0), /*status_fg*/RGBA(0x4C566A)
	};
}


static inline Palette
SolarizedDark()
{
	return {
		/*bg*/RGBA(0x002b36), /*fg*/RGBA(0x93a1a1), /*sel_bg*/RGBA(0x586e75, 0.40f),
		/*cur_bg*/RGBA(0x657b83, 0.35f), /*status_bg*/RGBA(0x073642), /*status_fg*/RGBA(0xeee8d5)
	};
}


static inline Palette
SolarizedLight()
{
	return {
		/*bg*/RGBA(0xfdf6e3), /*fg*/RGBA(0x586e75), /*sel_bg*/RGBA(0x268bd2, 0.25f),
		/*cur_bg*/RGBA(0x000000, 0.10f), /*status_bg*/RGBA(0xeee8d5), /*status_fg*/RGBA(0x657b83)
	};
}


static inline Palette
GruvboxDark()
{
	return {
		/*bg*/RGBA(0x282828), /*fg*/RGBA(0xebdbb2), /*sel_bg*/RGBA(0xd79921, 0.35f),
		/*cur_bg*/RGBA(0x458588, 0.40f), /*status_bg*/RGBA(0x3c3836), /*status_fg*/RGBA(0xd5c4a1)
	};
}


static inline Palette
GruvboxLight()
{
	return {
		/*bg*/RGBA(0xfbf1c7), /*fg*/RGBA(0x3c3836), /*sel_bg*/RGBA(0x076678, 0.22f),
		/*cur_bg*/RGBA(0x000000, 0.10f), /*status_bg*/RGBA(0xebdbb2), /*status_fg*/RGBA(0x504945)
	};
}


static inline Palette
EInk()
{
	return {
		/*bg*/RGBA(0xffffff), /*fg*/RGBA(0x000000), /*sel_bg*/RGBA(0x000000, 0.10f),
		/*cur_bg*/RGBA(0x000000, 0.12f), /*status_bg*/RGBA(0x000000), /*status_fg*/RGBA(0xffffff)
	};
}


// Apply a Qt theme by name. Returns true on success. Name matching is case-insensitive and
// supports common aliases (e.g., "solarized-light" or "solarized light"). If the name conveys
// a background (light/dark), BackgroundMode is updated to keep SyntaxInk consistent.
static inline bool
ApplyQtThemeByName(std::string name)
{
	// normalize
	std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
		return (char) std::tolower(c);
	});
	auto has = [&](const std::string &s) {
		return name.find(s) != std::string::npos;
	};

	if (name.empty() || name == "default" || name == "nord") {
		// Choose variant by current background mode
		if (GetBackgroundMode() == BackgroundMode::Dark) {
			gOverridePalette = NordDark();
		} else {
			gOverridePalette = NordLight();
		}
		gPaletteOverride   = true;
		gOverrideThemeName = "nord";
		return true;
	}

	if (has("solarized")) {
		if (has("light")) {
			SetBackgroundMode(BackgroundMode::Light);
			gOverridePalette = SolarizedLight();
		} else if (has("dark")) {
			SetBackgroundMode(BackgroundMode::Dark);
			gOverridePalette = SolarizedDark();
		} else {
			// pick from current background
			gOverridePalette = (GetBackgroundMode() == BackgroundMode::Dark)
				                   ? SolarizedDark()
				                   : SolarizedLight();
		}
		gPaletteOverride   = true;
		gOverrideThemeName = "solarized";
		return true;
	}

	if (has("gruvbox")) {
		if (has("light")) {
			SetBackgroundMode(BackgroundMode::Light);
			gOverridePalette = GruvboxLight();
		} else if (has("dark")) {
			SetBackgroundMode(BackgroundMode::Dark);
			gOverridePalette = GruvboxDark();
		} else {
			gOverridePalette = (GetBackgroundMode() == BackgroundMode::Dark)
				                   ? GruvboxDark()
				                   : GruvboxLight();
		}
		gPaletteOverride   = true;
		gOverrideThemeName = "gruvbox";
		return true;
	}

	if (has("eink") || has("e-ink") || has("paper")) {
		SetBackgroundMode(BackgroundMode::Light);
		gOverridePalette   = EInk();
		gPaletteOverride   = true;
		gOverrideThemeName = "eink";
		return true;
	}

	// Unknown -> clear override so default light/dark applies; return false.
	gPaletteOverride = false;
	gOverrideThemeName.clear();
	return false;
}


// Minimal SyntaxInk mapping for Qt builds, returning KteColor
[[maybe_unused]] static KteColor
SyntaxInk(const TokenKind k)
{
	const bool dark    = (GetBackgroundMode() == BackgroundMode::Dark);
	const KteColor def = dark ? RGBA(0xD8DEE9) : RGBA(0x2E3440);
	switch (k) {
	case TokenKind::Keyword:
		return dark ? RGBA(0x81A1C1) : RGBA(0x5E81AC);
	case TokenKind::Type:
		return dark ? RGBA(0x8FBCBB) : RGBA(0x4C566A);
	case TokenKind::String:
		return dark ? RGBA(0xA3BE8C) : RGBA(0x6C8E5E);
	case TokenKind::Char:
		return dark ? RGBA(0xA3BE8C) : RGBA(0x6C8E5E);
	case TokenKind::Comment:
		return dark ? RGBA(0x616E88) : RGBA(0x7A869A);
	case TokenKind::Number:
		return dark ? RGBA(0xEBCB8B) : RGBA(0xB58900);
	case TokenKind::Preproc:
		return dark ? RGBA(0xD08770) : RGBA(0xAF3A03);
	case TokenKind::Constant:
		return dark ? RGBA(0xB48EAD) : RGBA(0x7B4B7F);
	case TokenKind::Function:
		return dark ? RGBA(0x88C0D0) : RGBA(0x3465A4);
	case TokenKind::Operator:
		return dark ? RGBA(0x2E3440) : RGBA(0x2E3440);
	case TokenKind::Punctuation:
		return dark ? RGBA(0xECEFF4) : RGBA(0x2E3440);
	case TokenKind::Identifier:
		return def;
	case TokenKind::Whitespace:
		return def;
	case TokenKind::Error:
		return dark ? RGBA(0xBF616A) : RGBA(0xCC0000);
	case TokenKind::Default: default:
		return def;
	}
}
} // namespace kte

#else

#include <imgui.h>
#include <vector>
#include <memory>
#include <string>
#include <algorithm>
#include <cctype>

#include "themes/ThemeHelpers.h"

namespace kte {
// Background mode selection for light/dark palettes
enum class BackgroundMode { Light, Dark };

// Global background mode; default to Dark to match prior defaults
inline auto gBackgroundMode = BackgroundMode::Dark;

// Basic theme identifier (kept minimal; some ids are aliases)
enum class ThemeId {
	EInk = 0,
	GruvboxDarkMedium = 1,
	GruvboxLightMedium = 1, // alias to unified gruvbox index
	Nord = 2,
	Plan9 = 3,
	Solarized = 4,
	Everforest = 5,
	KanagawaPaper = 6,
	LCARS = 7,
	OldBook = 8,
	Zenburn = 9,
	Amber = 10,
	WeylandYutani = 11,
	Orbital = 12,
	Tufte = 13,
	Leuchtturm = 14,
};

// Current theme tracking
inline auto gCurrentTheme             = ThemeId::Nord;
inline std::size_t gCurrentThemeIndex = 7; // Nord index

// Forward declarations for helpers used below
static size_t ThemeIndexFromId(ThemeId id);

static ThemeId ThemeIdFromIndex(size_t idx);

// Helpers to set/query background mode
static void
SetBackgroundMode(const BackgroundMode m)
{
	gBackgroundMode = m;
}


static BackgroundMode
GetBackgroundMode()
{
	return gBackgroundMode;
}


static inline const char *
BackgroundModeName()
{
	return gBackgroundMode == BackgroundMode::Light ? "light" : "dark";
}


// Include individual theme implementations split under ./themes
#include "themes/Nord.h"
#include "themes/Plan9.h"
#include "themes/Solarized.h"
#include "themes/Gruvbox.h"
#include "themes/EInk.h"
#include "themes/Everforest.h"
#include "themes/KanagawaPaper.h"
#include "themes/LCARS.h"
#include "themes/Leuchtturm.h"
#include "themes/OldBook.h"
#include "themes/Amber.h"
#include "themes/WeylandYutani.h"
#include "themes/Zenburn.h"
#include "themes/Orbital.h"
#include "themes/Tufte.h"


// Theme abstraction and registry (generalized theme system)
class Theme {
public:
	virtual ~Theme() = default;

	[[nodiscard]] virtual const char *Name() const = 0; // canonical name (e.g., "nord", "gruvbox-dark")
	virtual void Apply() const = 0; // apply to current ImGui style
	virtual ThemeId Id() = 0; // theme identifier
};

namespace detail {
struct LCARSTheme final : Theme {
	[[nodiscard]] const char *Name() const override
	{
		return "lcars";
	}


	void Apply() const override
	{
		ApplyLcarsTheme();
	}


	ThemeId Id() override
	{
		return ThemeId::LCARS;
	}
};

struct LeuchtturmTheme final : Theme {
	[[nodiscard]] const char *Name() const override
	{
		return "leuchtturm";
	}


	void Apply() const override
	{
		if (gBackgroundMode == BackgroundMode::Dark)
			ApplyLeuchtturmDarkTheme();
		else
			ApplyLeuchtturmLightTheme();
	}


	ThemeId Id() override
	{
		return ThemeId::Leuchtturm;
	}
};

struct EverforestTheme final : Theme {
	[[nodiscard]] const char *Name() const override
	{
		return "everforest";
	}


	void Apply() const override
	{
		ApplyEverforestTheme();
	}


	ThemeId Id() override
	{
		return ThemeId::Everforest;
	}
};

struct KanagawaPaperTheme final : Theme {
	[[nodiscard]] const char *Name() const override
	{
		return "kanagawa-paper";
	}


	void Apply() const override
	{
		ApplyKanagawaPaperTheme();
	}


	ThemeId Id() override
	{
		return ThemeId::KanagawaPaper;
	}
};

struct OldBookTheme final : Theme {
	[[nodiscard]] const char *Name() const override
	{
		return "old-book";
	}


	void Apply() const override
	{
		if (gBackgroundMode == BackgroundMode::Dark)
			ApplyOldBookDarkTheme();
		else
			ApplyOldBookLightTheme();
	}


	ThemeId Id() override
	{
		return ThemeId::OldBook;
	}
};

struct OrbitalTheme final : Theme {
	[[nodiscard]] const char *Name() const override
	{
		return "orbital";
	}


	void Apply() const override
	{
		ApplyOrbitalTheme();
	}


	ThemeId Id() override
	{
		return ThemeId::Orbital;
	}
};

struct TufteTheme final : Theme {
	[[nodiscard]] const char *Name() const override
	{
		return "tufte";
	}


	void Apply() const override
	{
		if (gBackgroundMode == BackgroundMode::Dark)
			ApplyTufteDarkTheme();
		else
			ApplyTufteLightTheme();
	}


	ThemeId Id() override
	{
		return ThemeId::Tufte;
	}
};

struct ZenburnTheme final : Theme {
	[[nodiscard]] const char *Name() const override
	{
		return "zenburn";
	}


	void Apply() const override
	{
		ApplyZenburnTheme();
	}


	ThemeId Id() override
	{
		return ThemeId::Zenburn;
	}
};

struct NordTheme final : Theme {
	[[nodiscard]] const char *Name() const override
	{
		return "nord";
	}


	void Apply() const override
	{
		ApplyNordImGuiTheme();
	}


	ThemeId Id() override
	{
		return ThemeId::Nord;
	}
};

struct AmberTheme final : Theme {
	[[nodiscard]] const char *Name() const override
	{
		return "amber";
	}


	void Apply() const override
	{
		ApplyAmberTheme();
	}


	ThemeId Id() override
	{
		return ThemeId::Amber;
	}
};

struct WeylandYutaniTheme final : Theme {
	[[nodiscard]] const char *Name() const override
	{
		return "weyland-yutani";
	}


	void Apply() const override
	{
		ApplyWeylandYutaniTheme();
	}


	ThemeId Id() override
	{
		return ThemeId::WeylandYutani;
	}
};

struct GruvboxTheme final : Theme {
	[[nodiscard]] const char *Name() const override
	{
		return "gruvbox";
	}


	void Apply() const override
	{
		if (gBackgroundMode == BackgroundMode::Light)
			ApplyGruvboxLightMediumTheme();
		else
			ApplyGruvboxDarkMediumTheme();
	}


	ThemeId Id() override
	{
		// Legacy maps to dark; unified under base id GruvboxDarkMedium
		return ThemeId::GruvboxDarkMedium;
	}
};

struct EInkTheme final : Theme {
	[[nodiscard]] const char *Name() const override
	{
		return "eink";
	}


	void Apply() const override
	{
		if (gBackgroundMode == BackgroundMode::Dark)
			ApplyEInkDarkImGuiTheme();
		else
			ApplyEInkImGuiTheme();
	}


	ThemeId Id() override
	{
		return ThemeId::EInk;
	}
};

struct SolarizedTheme final : Theme {
	[[nodiscard]] const char *Name() const override
	{
		return "solarized";
	}


	void Apply() const override
	{
		if (gBackgroundMode == BackgroundMode::Light)
			ApplySolarizedLightTheme();
		else
			ApplySolarizedDarkTheme();
	}


	ThemeId Id() override
	{
		return ThemeId::Solarized;
	}
};

struct Plan9Theme final : Theme {
	[[nodiscard]] const char *Name() const override
	{
		return "plan9";
	}


	void Apply() const override
	{
		ApplyPlan9Theme();
	}


	ThemeId Id() override
	{
		return ThemeId::Plan9;
	}
};
} // namespace detail

static const std::vector<std::unique_ptr<Theme> > &
ThemeRegistry()
{
	static std::vector<std::unique_ptr<Theme> > reg;
	if (reg.empty()) {
		// Alphabetical by canonical name:
		// amber, eink, everforest, gruvbox, kanagawa-paper, lcars, leuchtturm, nord, old-book, orbital, plan9, solarized, tufte, weyland-yutani, zenburn
		reg.emplace_back(std::make_unique<detail::AmberTheme>());
		reg.emplace_back(std::make_unique<detail::EInkTheme>());
		reg.emplace_back(std::make_unique<detail::EverforestTheme>());
		reg.emplace_back(std::make_unique<detail::GruvboxTheme>());
		reg.emplace_back(std::make_unique<detail::KanagawaPaperTheme>());
		reg.emplace_back(std::make_unique<detail::LCARSTheme>());
		reg.emplace_back(std::make_unique<detail::LeuchtturmTheme>());
		reg.emplace_back(std::make_unique<detail::NordTheme>());
		reg.emplace_back(std::make_unique<detail::OldBookTheme>());
		reg.emplace_back(std::make_unique<detail::OrbitalTheme>());
		reg.emplace_back(std::make_unique<detail::Plan9Theme>());
		reg.emplace_back(std::make_unique<detail::SolarizedTheme>());
		reg.emplace_back(std::make_unique<detail::TufteTheme>());
		reg.emplace_back(std::make_unique<detail::WeylandYutaniTheme>());
		reg.emplace_back(std::make_unique<detail::ZenburnTheme>());
	}
	return reg;
}


// Canonical theme name for a given ThemeId (via registry order)
[[maybe_unused]] static const char *
ThemeName(const ThemeId id)
{
	const auto &reg  = ThemeRegistry();
	const size_t idx = ThemeIndexFromId(id);
	if (idx < reg.size())
		return reg[idx]->Name();
	return "unknown";
}


// Helper to apply a theme by id and update current theme
static void
ApplyTheme(const ThemeId id)
{
	const auto &reg  = ThemeRegistry();
	const size_t idx = ThemeIndexFromId(id);
	if (idx < reg.size()) {
		reg[idx]->Apply();
		gCurrentTheme      = id;
		gCurrentThemeIndex = idx;
	}
}


[[maybe_unused]] static ThemeId
CurrentTheme()
{
	return gCurrentTheme;
}


// Cycle helpers
[[maybe_unused]] static ThemeId
NextTheme()
{
	const auto &reg = ThemeRegistry();
	if (reg.empty()) {
		return gCurrentTheme;
	}

	const size_t nxt = (gCurrentThemeIndex + 1) % reg.size();
	ApplyTheme(ThemeIdFromIndex(nxt));
	return gCurrentTheme;
}


[[maybe_unused]] static ThemeId
PrevTheme()
{
	const auto &reg = ThemeRegistry();
	if (reg.empty()) {
		return gCurrentTheme;
	}

	const size_t prv = (gCurrentThemeIndex + reg.size() - 1) % reg.size();
	ApplyTheme(ThemeIdFromIndex(prv));
	return gCurrentTheme;
}


// Name-based API
[[maybe_unused]] static const Theme *
GetThemeByName(const std::string &name)
{
	const auto &reg = ThemeRegistry();
	for (const auto &t: reg) {
		if (name == t->Name())
			return t.get();
	}

	return nullptr;
}


[[maybe_unused]] static bool
ApplyThemeByName(const std::string &name)
{
	// Handle aliases and background-specific names
	std::string n = name;
	// lowercase copy
	std::transform(n.begin(), n.end(), n.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});

	if (n == "gruvbox-dark") {
		SetBackgroundMode(BackgroundMode::Dark);
		n = "gruvbox";
	} else if (n == "gruvbox-light") {
		SetBackgroundMode(BackgroundMode::Light);
		n = "gruvbox";
	} else if (n == "solarized-dark") {
		SetBackgroundMode(BackgroundMode::Dark);
		n = "solarized";
	} else if (n == "solarized-light") {
		SetBackgroundMode(BackgroundMode::Light);
		n = "solarized";
	} else if (n == "eink-dark") {
		SetBackgroundMode(BackgroundMode::Dark);
		n = "eink";
	} else if (n == "eink-light") {
		SetBackgroundMode(BackgroundMode::Light);
		n = "eink";
	} else if (n == "everforest-hard") {
		// Request asks for everforest hard; map to canonical name
		n = "everforest";
	} else if (n == "oldbook") {
		// alias to old-book
		n = "old-book";
	} else if (n == "old-book-dark" || n == "oldbook-dark") {
		SetBackgroundMode(BackgroundMode::Dark);
		n = "old-book";
	} else if (n == "old-book-light" || n == "oldbook-light") {
		SetBackgroundMode(BackgroundMode::Light);
		n = "old-book";
	} else if (n == "kanagawa" || n == "kanagawa-paper-light" || n == "kanagawa-light"
	           || n == "kanagawa-dark" || n == "kanagawa-paper-dark") {
		// map to canonical kanagawa-paper; background controls light/dark
		n = "kanagawa-paper";
	} else if (n == "vim-amber") {
		n = "amber";
	} else if (n == "weyland") {
		n = "weyland-yutani";
	}

	const auto &reg = ThemeRegistry();
	for (size_t i = 0; i < reg.size(); ++i) {
		if (n == reg[i]->Name()) {
			reg[i]->Apply();
			gCurrentThemeIndex = i;
			gCurrentTheme      = ThemeIdFromIndex(i);
			return true;
		}
	}

	return false;
}


[[maybe_unused]] static const char *
CurrentThemeName()
{
	const auto &reg = ThemeRegistry();
	if (gCurrentThemeIndex < reg.size()) {
		return reg[gCurrentThemeIndex]->Name();
	}

	return "unknown";
}


// Helpers to map between legacy ThemeId and registry index
static size_t
ThemeIndexFromId(const ThemeId id)
{
	switch (id) {
	case ThemeId::Amber:
		return 0;
	case ThemeId::EInk:
		return 1;
	case ThemeId::Everforest:
		return 2;
	case ThemeId::GruvboxDarkMedium:
		return 3;
	case ThemeId::KanagawaPaper:
		return 4;
	case ThemeId::LCARS:
		return 5;
	case ThemeId::Leuchtturm:
		return 6;
	case ThemeId::Nord:
		return 7;
	case ThemeId::OldBook:
		return 8;
	case ThemeId::Orbital:
		return 9;
	case ThemeId::Plan9:
		return 10;
	case ThemeId::Solarized:
		return 11;
	case ThemeId::Tufte:
		return 12;
	case ThemeId::WeylandYutani:
		return 13;
	case ThemeId::Zenburn:
		return 14;
	}
	return 0;
}


static ThemeId
ThemeIdFromIndex(const size_t idx)
{
	switch (idx) {
	default:
	case 0:
		return ThemeId::Amber;
	case 1:
		return ThemeId::EInk;
	case 2:
		return ThemeId::Everforest;
	case 3:
		return ThemeId::GruvboxDarkMedium; // unified gruvbox
	case 4:
		return ThemeId::KanagawaPaper;
	case 5:
		return ThemeId::LCARS;
	case 6:
		return ThemeId::Leuchtturm;
	case 7:
		return ThemeId::Nord;
	case 8:
		return ThemeId::OldBook;
	case 9:
		return ThemeId::Orbital;
	case 10:
		return ThemeId::Plan9;
	case 11:
		return ThemeId::Solarized;
	case 12:
		return ThemeId::Tufte;
	case 13:
		return ThemeId::WeylandYutani;
	case 14:
		return ThemeId::Zenburn;
	}
}


// --- Syntax palette (v1): map TokenKind to ink color per current theme/background ---

// Tufte palette: high-contrast, restrained color. Body text is true black on
// cream; only keywords and links get subtle color to avoid a "christmas tree."
static ImVec4
SyntaxInkTufte(const TokenKind k, const bool dark)
{
	const ImVec4 ink  = dark ? RGBA(0xEAE6DE) : RGBA(0x111111); // body text
	const ImVec4 dim  = dark ? RGBA(0x8A8680) : RGBA(0x555555); // comments
	const ImVec4 red  = dark ? RGBA(0xD06060) : RGBA(0x8B0000); // keywords/preproc
	const ImVec4 navy = dark ? RGBA(0x7098C0) : RGBA(0x1A3A5C); // functions/links
	const ImVec4 grn  = dark ? RGBA(0x8AAA6E) : RGBA(0x2E5E2E); // strings
	switch (k) {
	case TokenKind::Keyword:
	case TokenKind::Preproc:
		return red;
	case TokenKind::String:
	case TokenKind::Char:
		return grn;
	case TokenKind::Comment:
		return dim;
	case TokenKind::Function:
		return navy;
	case TokenKind::Number:
	case TokenKind::Constant:
		return dark ? RGBA(0xC8A85A) : RGBA(0x6B4C00);
	case TokenKind::Type:
		return dark ? RGBA(0xBBAA90) : RGBA(0x333333);
	case TokenKind::Error:
		return dark ? RGBA(0xD06060) : RGBA(0xCC0000);
	default:
		return ink;
	}
}


// Leuchtturm palette: blue-black fountain pen ink with brass and bronze accents.
// Body text is ink-colored; accents drawn from the pen metals.
static ImVec4
SyntaxInkLeuchtturm(const TokenKind k, const bool dark)
{
	const ImVec4 ink   = dark ? RGBA(0xE5DDD0) : RGBA(0x040720); // fountain pen ink
	const ImVec4 dim   = dark ? RGBA(0x7A7060) : RGBA(0x6A6558); // comments
	const ImVec4 brass = dark ? RGBA(0xB8A060) : RGBA(0x504518); // patinated brass
	const ImVec4 bronze= dark ? RGBA(0xC08050) : RGBA(0x5C3010); // dark bronze
	const ImVec4 navy  = dark ? RGBA(0x8898B0) : RGBA(0x1C2E4A); // deep navy
	switch (k) {
	case TokenKind::Keyword:
	case TokenKind::Preproc:
		return brass;
	case TokenKind::String:
	case TokenKind::Char:
		return bronze;
	case TokenKind::Comment:
		return dim;
	case TokenKind::Function:
		return navy;
	case TokenKind::Number:
	case TokenKind::Constant:
		return dark ? RGBA(0xA89060) : RGBA(0x483C10);
	case TokenKind::Type:
		return dark ? RGBA(0xC0B898) : RGBA(0x222238);
	case TokenKind::Error:
		return dark ? RGBA(0xD06060) : RGBA(0xA02020);
	default:
		return ink;
	}
}


// Everforest: warm forest palette on dark green-gray (bg 0x2B3339).
// Default comment color (0x616E88) is too dim; boost it and tune others.
static ImVec4
SyntaxInkEverforest(const TokenKind k)
{
	switch (k) {
	case TokenKind::Keyword:
		return RGBA(0xE67E80); // everforest red
	case TokenKind::Type:
		return RGBA(0xD699B6); // everforest purple
	case TokenKind::String:
	case TokenKind::Char:
		return RGBA(0xA7C080); // everforest green
	case TokenKind::Comment:
		return RGBA(0x859289); // boosted from 0x616E88 for contrast
	case TokenKind::Number:
	case TokenKind::Constant:
		return RGBA(0xD8A657); // everforest yellow/orange
	case TokenKind::Preproc:
		return RGBA(0xE69875); // everforest orange
	case TokenKind::Function:
		return RGBA(0x83C092); // everforest aqua
	case TokenKind::Operator:
	case TokenKind::Punctuation:
		return RGBA(0xD3C6AA); // everforest fg
	case TokenKind::Error:
		return RGBA(0xE67E80);
	default:
		return RGBA(0xD3C6AA); // everforest fg
	}
}


[[maybe_unused]] static ImVec4
SyntaxInk(const TokenKind k)
{
	const bool dark = (GetBackgroundMode() == BackgroundMode::Dark);

	// Per-theme syntax palettes
	if (gCurrentTheme == ThemeId::Tufte)
		return SyntaxInkTufte(k, dark);
	if (gCurrentTheme == ThemeId::Leuchtturm)
		return SyntaxInkLeuchtturm(k, dark);
	if (gCurrentTheme == ThemeId::Everforest)
		return SyntaxInkEverforest(k);

	// Default palettes tuned for Nord-ish themes
	const ImVec4 def = dark ? RGBA(0xD8DEE9) : RGBA(0x2E3440);
	switch (k) {
	case TokenKind::Keyword:
		return dark ? RGBA(0x81A1C1) : RGBA(0x5E81AC);
	case TokenKind::Type:
		return dark ? RGBA(0x8FBCBB) : RGBA(0x4C566A);
	case TokenKind::String:
		return dark ? RGBA(0xA3BE8C) : RGBA(0x6C8E5E);
	case TokenKind::Char:
		return dark ? RGBA(0xA3BE8C) : RGBA(0x6C8E5E);
	case TokenKind::Comment:
		return dark ? RGBA(0x616E88) : RGBA(0x7A869A);
	case TokenKind::Number:
		return dark ? RGBA(0xEBCB8B) : RGBA(0xB58900);
	case TokenKind::Preproc:
		return dark ? RGBA(0xD08770) : RGBA(0xAF3A03);
	case TokenKind::Constant:
		return dark ? RGBA(0xB48EAD) : RGBA(0x7B4B7F);
	case TokenKind::Function:
		return dark ? RGBA(0x88C0D0) : RGBA(0x3465A4);
	case TokenKind::Operator:
		return dark ? RGBA(0xECEFF4) : RGBA(0x2E3440);
	case TokenKind::Punctuation:
		return dark ? RGBA(0xECEFF4) : RGBA(0x2E3440);
	case TokenKind::Identifier:
		return def;
	case TokenKind::Whitespace:
		return def;
	case TokenKind::Error:
		return dark ? RGBA(0xBF616A) : RGBA(0xCC0000);
	case TokenKind::Default: default:
		return def;
	}
}
} // namespace kte

#endif // KTE_USE_QT
