// GUITheme.h — theming helpers and background mode
#pragma once

#include <cstddef>
#include <string>
#include <algorithm>
#include <cctype>

#include "Highlight.h"

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


// Amber: monochrome CRT amber/black with warm parchment text; cool teal/cyan
// accents provide contrast against the otherwise all-amber palette.
static ImVec4
SyntaxInkAmber(const TokenKind k)
{
	const ImVec4 text   = RGBA(0xE6D5A3);
	const ImVec4 amber  = RGBA(0xFFB000);
	const ImVec4 orange = RGBA(0xFF8C32);
	const ImVec4 yellow = RGBA(0xFFD166);
	const ImVec4 teal   = RGBA(0x3AAFA9);
	const ImVec4 cyan   = RGBA(0x4CC9F0);
	switch (k) {
	case TokenKind::Keyword:
	case TokenKind::Preproc:
		return amber;
	case TokenKind::Type:
		return yellow;
	case TokenKind::String:
	case TokenKind::Char:
		return teal;
	case TokenKind::Comment:
		return ImVec4(text.x, text.y, text.z, 0.50f);
	case TokenKind::Number:
	case TokenKind::Constant:
		return orange;
	case TokenKind::Function:
		return cyan;
	case TokenKind::Error:
		return RGBA(0xFF5555);
	default:
		return text;
	}
}


// E-Ink: near-monochrome, restrained spot color (like Tufte) rather than a
// full rainbow, to match the grayscale reading-device aesthetic.
static ImVec4
SyntaxInkEInk(const TokenKind k, const bool dark)
{
	const ImVec4 ink   = dark ? RGBA(0xEDEDEA) : RGBA(0x111111);
	const ImVec4 dim   = dark ? RGBA(0xB5B5B3) : RGBA(0x666666);
	const ImVec4 navy  = dark ? RGBA(0x86A6D6) : RGBA(0x2A4B7C);
	const ImVec4 brick = dark ? RGBA(0xD98080) : RGBA(0x8B3A3A);
	switch (k) {
	case TokenKind::Keyword:
	case TokenKind::Preproc:
		return navy;
	case TokenKind::String:
	case TokenKind::Char:
		return brick;
	case TokenKind::Comment:
		return dim;
	case TokenKind::Function:
		return navy;
	case TokenKind::Number:
	case TokenKind::Constant:
		return dark ? RGBA(0xC8B27A) : RGBA(0x6B4C00);
	case TokenKind::Error:
		return brick;
	default:
		return ink;
	}
}


// Gruvbox: authentic "retro groove" highlight-group mapping (red statements,
// yellow types, green strings, gray comments, purple constants).
static ImVec4
SyntaxInkGruvbox(const TokenKind k, const bool dark)
{
	const ImVec4 fg     = dark ? RGBA(0xEBDBB2) : RGBA(0x3C3836);
	const ImVec4 gray   = dark ? RGBA(0x928374) : RGBA(0x7C6F64);
	const ImVec4 red    = dark ? RGBA(0xFB4934) : RGBA(0x9D0006);
	const ImVec4 green  = dark ? RGBA(0xB8BB26) : RGBA(0x79740E);
	const ImVec4 yellow = dark ? RGBA(0xFABD2F) : RGBA(0xB57614);
	const ImVec4 aqua   = dark ? RGBA(0x8EC07C) : RGBA(0x427B58);
	const ImVec4 purple = dark ? RGBA(0xD3869B) : RGBA(0x8F3F71);
	const ImVec4 blue   = dark ? RGBA(0x83A598) : RGBA(0x076678);
	switch (k) {
	case TokenKind::Keyword:
		return red;
	case TokenKind::Type:
		return yellow;
	case TokenKind::String:
	case TokenKind::Char:
		return green;
	case TokenKind::Comment:
		return gray;
	case TokenKind::Number:
	case TokenKind::Constant:
		return purple;
	case TokenKind::Preproc:
		return aqua;
	case TokenKind::Function:
		return blue;
	case TokenKind::Error:
		return red;
	default:
		return fg;
	}
}


// Kanagawa Paper: sumi-ink text with wave/autumn accents drawn from the
// Kanagawa vim colorscheme, muted for the paper-light variant.
static ImVec4
SyntaxInkKanagawaPaper(const TokenKind k, const bool dark)
{
	const ImVec4 fg     = dark ? RGBA(0xDCD7BA) : RGBA(0x3A3A3A);
	const ImVec4 gray   = dark ? RGBA(0x9C9A8F) : RGBA(0x8A8674);
	const ImVec4 violet = dark ? RGBA(0x957FB8) : RGBA(0x7D5C8A);
	const ImVec4 green  = dark ? RGBA(0x98BB6C) : RGBA(0x6F894E);
	const ImVec4 orange = dark ? RGBA(0xFFA066) : RGBA(0xC4746E);
	const ImVec4 blue   = dark ? RGBA(0x7E9CD8) : RGBA(0x487AA1);
	const ImVec4 red    = dark ? RGBA(0xE46876) : RGBA(0xC34043);
	switch (k) {
	case TokenKind::Keyword:
		return violet;
	case TokenKind::Type:
		return blue;
	case TokenKind::String:
	case TokenKind::Char:
		return green;
	case TokenKind::Comment:
		return gray;
	case TokenKind::Number:
	case TokenKind::Constant:
		return orange;
	case TokenKind::Preproc:
		return red;
	case TokenKind::Function:
		return blue;
	case TokenKind::Error:
		return red;
	default:
		return fg;
	}
}


// LCARS: vibrant Star Trek console pastels against near-black.
static ImVec4
SyntaxInkLCARS(const TokenKind k)
{
	const ImVec4 fg     = RGBA(0xECECEC);
	const ImVec4 dim    = RGBA(0xBFBFC4);
	const ImVec4 orange = RGBA(0xFF9966);
	const ImVec4 yellow = RGBA(0xFFCC66);
	const ImVec4 purple = RGBA(0xCC99FF);
	const ImVec4 blue   = RGBA(0x66CCFF);
	const ImVec4 green  = RGBA(0x99FFCC);
	switch (k) {
	case TokenKind::Keyword:
		return orange;
	case TokenKind::Type:
		return purple;
	case TokenKind::String:
	case TokenKind::Char:
		return green;
	case TokenKind::Comment:
		return dim;
	case TokenKind::Number:
	case TokenKind::Constant:
		return yellow;
	case TokenKind::Preproc:
		return RGBA(0xFF99CC); // lcars pink
	case TokenKind::Function:
		return blue;
	case TokenKind::Error:
		return RGBA(0xFF6666);
	default:
		return fg;
	}
}


// Old Book: sepia/dark mapping lifted from the oldbook8.vim highlight groups
// already used for the ImGui chrome, so code reads as part of the same book.
static ImVec4
SyntaxInkOldBook(const TokenKind k, const bool dark)
{
	const ImVec4 ink    = dark ? RGBA(0xD5D4D2) : RGBA(0x3B2F2F);
	const ImVec4 dim    = dark ? RGBA(0x626C77) : RGBA(0x5A4540);
	const ImVec4 red    = dark ? RGBA(0xDB6C6C) : RGBA(0xA65858);
	const ImVec4 teal   = dark ? RGBA(0x5BB899) : RGBA(0x3F7F7A);
	const ImVec4 blue   = dark ? RGBA(0x87D7FF) : RGBA(0x4F6D8C);
	const ImVec4 yellow = dark ? RGBA(0xDDD668) : RGBA(0xB58B00);
	switch (k) {
	case TokenKind::Keyword:
	case TokenKind::Preproc:
		return red;
	case TokenKind::Type:
		return blue;
	case TokenKind::String:
	case TokenKind::Char:
		return teal;
	case TokenKind::Comment:
		return dim;
	case TokenKind::Number:
	case TokenKind::Constant:
		return yellow;
	case TokenKind::Function:
		return blue;
	case TokenKind::Error:
		return red;
	default:
		return ink;
	}
}


// Orbital: deep-space dark with cool blues/cyans/magenta; a spot of green
// distinguishes strings from the otherwise blue-heavy palette.
static ImVec4
SyntaxInkOrbital(const TokenKind k)
{
	const ImVec4 fg      = RGBA(0xC8D3E5);
	const ImVec4 dim     = RGBA(0x9FB2CC);
	const ImVec4 blue    = RGBA(0x6AA2FF);
	const ImVec4 cyan    = RGBA(0x5AD1E6);
	const ImVec4 magenta = RGBA(0xD681F8);
	const ImVec4 yellow  = RGBA(0xE6C17D);
	const ImVec4 green   = RGBA(0x7EE081);
	switch (k) {
	case TokenKind::Keyword:
		return blue;
	case TokenKind::Type:
		return cyan;
	case TokenKind::String:
	case TokenKind::Char:
		return green;
	case TokenKind::Comment:
		return dim;
	case TokenKind::Number:
	case TokenKind::Constant:
		return yellow;
	case TokenKind::Preproc:
		return magenta;
	case TokenKind::Function:
		return cyan;
	case TokenKind::Error:
		return RGBA(0xFF6B6B);
	default:
		return fg;
	}
}


// Plan 9 / acme: deliberately restrained — black ink on paper with acme's
// signature blue for structure, olive for strings, brick for errors.
static ImVec4
SyntaxInkPlan9(const TokenKind k)
{
	const ImVec4 ink   = RGBA(0x000000);
	const ImVec4 blueH = RGBA(0x4C8DFF);
	switch (k) {
	case TokenKind::Keyword:
	case TokenKind::Preproc:
		return RGBA(0x0064FF); // acme blue
	case TokenKind::Type:
	case TokenKind::Function:
	case TokenKind::Number:
	case TokenKind::Constant:
		return blueH;
	case TokenKind::String:
	case TokenKind::Char:
		return RGBA(0x7A6A00); // olive
	case TokenKind::Comment:
		return ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
	case TokenKind::Error:
		return RGBA(0xB33A3A);
	default:
		return ink;
	}
}


// Solarized: Ethan Schoonover's canonical accent-to-role mapping. Accent
// hues are shared between light/dark by design; only body/comment shift.
static ImVec4
SyntaxInkSolarized(const TokenKind k, const bool dark)
{
	const ImVec4 body    = dark ? RGBA(0x839496) : RGBA(0x657b83);
	const ImVec4 comment = RGBA(0x586e75); // base01
	const ImVec4 yellow  = RGBA(0xb58900);
	const ImVec4 orange  = RGBA(0xcb4b16);
	const ImVec4 red     = RGBA(0xdc322f);
	const ImVec4 blue    = RGBA(0x268bd2);
	const ImVec4 cyan    = RGBA(0x2aa198);
	const ImVec4 green   = RGBA(0x859900);
	const ImVec4 violet  = RGBA(0x6c71c4);
	switch (k) {
	case TokenKind::Keyword:
		return green;
	case TokenKind::Type:
		return yellow;
	case TokenKind::String:
	case TokenKind::Char:
		return cyan;
	case TokenKind::Comment:
		return comment;
	case TokenKind::Number:
	case TokenKind::Constant:
		return violet;
	case TokenKind::Preproc:
		return orange;
	case TokenKind::Function:
		return blue;
	case TokenKind::Error:
		return red;
	default:
		return body;
	}
}


// Weyland-Yutani: corporate terminal black with hazard yellow/orange and a
// system-green for strings so the palette isn't purely amber-on-black.
static ImVec4
SyntaxInkWeylandYutani(const TokenKind k)
{
	const ImVec4 fg     = RGBA(0xE6E6E6);
	const ImVec4 dim    = RGBA(0xBDBDBD);
	const ImVec4 hazard = RGBA(0xFFC300);
	const ImVec4 orange = RGBA(0xFF8C00);
	const ImVec4 cyan   = RGBA(0x4CC9F0);
	const ImVec4 green  = RGBA(0x6FCF97);
	switch (k) {
	case TokenKind::Keyword:
	case TokenKind::Preproc:
		return hazard;
	case TokenKind::Type:
		return cyan;
	case TokenKind::String:
	case TokenKind::Char:
		return green;
	case TokenKind::Comment:
		return dim;
	case TokenKind::Number:
	case TokenKind::Constant:
		return orange;
	case TokenKind::Function:
		return cyan;
	case TokenKind::Error:
		return RGBA(0xFF4444);
	default:
		return fg;
	}
}


// Zenburn: low-contrast dark palette, faces lifted from zenburn.vim
// (dim green comments, dusty-rose strings/constants, pale yellow keywords).
static ImVec4
SyntaxInkZenburn(const TokenKind k)
{
	const ImVec4 fg      = RGBA(0xDCDCCC);
	const ImVec4 comment = RGBA(0x7F9F7F);
	const ImVec4 str     = RGBA(0xCC9393);
	const ImVec4 keyword = RGBA(0xF0DFAF);
	const ImVec4 type    = RGBA(0xDFDFBF);
	const ImVec4 func    = RGBA(0xEFEF8F);
	const ImVec4 blue    = RGBA(0x8CD0D3);
	switch (k) {
	case TokenKind::Keyword:
		return keyword;
	case TokenKind::Type:
		return type;
	case TokenKind::String:
	case TokenKind::Char:
		return str;
	case TokenKind::Comment:
		return comment;
	case TokenKind::Number:
	case TokenKind::Constant:
		return str;
	case TokenKind::Preproc:
		return blue;
	case TokenKind::Function:
		return func;
	case TokenKind::Error:
		return RGBA(0xE37170);
	default:
		return fg;
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
	if (gCurrentTheme == ThemeId::Amber)
		return SyntaxInkAmber(k);
	if (gCurrentTheme == ThemeId::EInk)
		return SyntaxInkEInk(k, dark);
	if (gCurrentTheme == ThemeId::GruvboxDarkMedium)
		return SyntaxInkGruvbox(k, dark);
	if (gCurrentTheme == ThemeId::KanagawaPaper)
		return SyntaxInkKanagawaPaper(k, dark);
	if (gCurrentTheme == ThemeId::LCARS)
		return SyntaxInkLCARS(k);
	if (gCurrentTheme == ThemeId::OldBook)
		return SyntaxInkOldBook(k, dark);
	if (gCurrentTheme == ThemeId::Orbital)
		return SyntaxInkOrbital(k);
	if (gCurrentTheme == ThemeId::Plan9)
		return SyntaxInkPlan9(k);
	if (gCurrentTheme == ThemeId::Solarized)
		return SyntaxInkSolarized(k, dark);
	if (gCurrentTheme == ThemeId::WeylandYutani)
		return SyntaxInkWeylandYutani(k);
	if (gCurrentTheme == ThemeId::Zenburn)
		return SyntaxInkZenburn(k);
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
