/*
 * GUIConfig - loads simple GUI configuration from $HOME/.config/kte/kge.ini
 */
#ifndef KTE_GUI_CONFIG_H
#define KTE_GUI_CONFIG_H

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
	// Accepts: on/off/true/false/yes/no/1/0 in the ini file.
	bool syntax = true; // default: enabled

	// Load from default path: $HOME/.config/kte/kge.ini
	static GUIConfig Load();

	// Load from explicit path. Returns true if file existed and was parsed.
	bool LoadFromFile(const std::string &path);
};

#endif // KTE_GUI_CONFIG_H