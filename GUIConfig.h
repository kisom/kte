/*
 * GUIConfig - loads GUI configuration from $HOME/.config/kte/kge.toml
 *
 * Falls back to legacy kge.ini if no TOML config is found.
 */
#pragma once

#include <string>

#ifndef KTE_FONT_SIZE
#define KTE_FONT_SIZE 16.0f
#endif

class GUIConfig {
public:
	bool fullscreen   = false;
	int columns       = 80;
	int rows          = 42;
	float font_size   = (float) KTE_FONT_SIZE;
	std::string font  = "default";
	std::string theme = "nord";
	// Background mode for themes that support light/dark variants
	// Values: "dark" (default), "light"
	std::string background = "dark";

	// Default syntax highlighting state for GUI (kge): on/off
	bool syntax = true;

	// Per-mode font defaults
	std::string code_font    = "default";
	std::string writing_font = "crimsonpro";

	// Load from default paths: try kge.toml first, fall back to kge.ini
	static GUIConfig Load();

	// Load from explicit TOML path. Returns true if file existed and was parsed.
	bool LoadFromTOML(const std::string &path);

	// Load from explicit INI path (legacy). Returns true if file existed and was parsed.
	bool LoadFromINI(const std::string &path);
};
