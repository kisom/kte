#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <iostream>

#include "GUIConfig.h"

// toml++ for TOML config parsing
#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wall"
#  pragma GCC diagnostic ignored "-Wextra"
#endif

#include "ext/tomlplusplus/toml.hpp"

#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif


static void
trim(std::string &s)
{
	auto not_space = [](int ch) {
		return !std::isspace(ch);
	};
	s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
	s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
}


static std::string
config_dir()
{
	const char *home = std::getenv("HOME");
	if (!home || !*home)
		return {};
	return std::string(home) + "/.config/kte";
}


GUIConfig
GUIConfig::Load()
{
	GUIConfig cfg;
	std::string dir = config_dir();
	if (dir.empty())
		return cfg;

	// Try TOML first
	std::string toml_path = dir + "/kge.toml";
	if (cfg.LoadFromTOML(toml_path))
		return cfg;

	// Fall back to legacy INI
	std::string ini_path = dir + "/kge.ini";
	if (cfg.LoadFromINI(ini_path)) {
		std::cerr << "kge: loaded legacy kge.ini; consider migrating to kge.toml\n";
		return cfg;
	}

	return cfg;
}


bool
GUIConfig::LoadFromTOML(const std::string &path)
{
	if (!std::filesystem::exists(path))
		return false;

	toml::table tbl;
	try {
		tbl = toml::parse_file(path);
	} catch (const toml::parse_error &err) {
		std::cerr << "kge: TOML parse error in " << path << ": " << err.what() << "\n";
		return false;
	}

	// [window]
	if (auto win = tbl["window"].as_table()) {
		if (auto v = (*win)["fullscreen"].value<bool>())
			fullscreen = *v;
		if (auto v = (*win)["columns"].value<int64_t>()) {
			if (*v > 0) columns = static_cast<int>(*v);
		}
		if (auto v = (*win)["rows"].value<int64_t>()) {
			if (*v > 0) rows = static_cast<int>(*v);
		}
	}

	// [font]
	bool explicit_code_font    = false;
	bool explicit_writing_font = false;
	if (auto sec = tbl["font"].as_table()) {
		if (auto v = (*sec)["name"].value<std::string>())
			font = *v;
		if (auto v = (*sec)["size"].value<double>()) {
			if (*v > 0.0) font_size = static_cast<float>(*v);
		}
		if (auto v = (*sec)["code"].value<std::string>()) {
			code_font = *v;
			explicit_code_font = true;
		}
		if (auto v = (*sec)["writing"].value<std::string>()) {
			writing_font = *v;
			explicit_writing_font = true;
		}
	}

	// [appearance]
	if (auto sec = tbl["appearance"].as_table()) {
		if (auto v = (*sec)["theme"].value<std::string>())
			theme = *v;
		if (auto v = (*sec)["background"].value<std::string>()) {
			std::string bg = *v;
			std::transform(bg.begin(), bg.end(), bg.begin(), [](unsigned char c) {
				return (char) std::tolower(c);
			});
			if (bg == "light" || bg == "dark")
				background = bg;
		}
	}

	// [editor]
	if (auto sec = tbl["editor"].as_table()) {
		if (auto v = (*sec)["syntax"].value<bool>())
			syntax = *v;
	}

	// Default code_font to the main font if not explicitly set
	if (!explicit_code_font)
		code_font = font;
	if (!explicit_writing_font && writing_font == "crimsonpro" && font != "default")
		writing_font = font;

	return true;
}


bool
GUIConfig::LoadFromINI(const std::string &path)
{
	std::ifstream in(path);
	if (!in.good())
		return false;

	bool explicit_code_font    = false;
	bool explicit_writing_font = false;

	std::string line;
	while (std::getline(in, line)) {
		// Remove comments starting with '#' or ';'
		auto hash = line.find('#');
		if (hash != std::string::npos)
			line.erase(hash);
		auto sc = line.find("//");
		if (sc != std::string::npos)
			line.erase(sc);
		// Basic key=value
		auto eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		std::string key = line.substr(0, eq);
		std::string val = line.substr(eq + 1);
		trim(key);
		trim(val);
		// Strip trailing semicolon
		if (!val.empty() && val.back() == ';') {
			val.pop_back();
			trim(val);
		}

		// Lowercase key
		std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
			return (char) std::tolower(c);
		});

		if (key == "fullscreen") {
			fullscreen = (val == "1" || val == "true" || val == "on" || val == "yes");
		} else if (key == "columns" || key == "cols") {
			int v = columns;
			try {
				v = std::stoi(val);
			} catch (...) {}
			if (v > 0)
				columns = v;
		} else if (key == "rows") {
			int v = rows;
			try {
				v = std::stoi(val);
			} catch (...) {}
			if (v > 0)
				rows = v;
		} else if (key == "font_size" || key == "fontsize") {
			float v = font_size;
			try {
				v = std::stof(val);
			} catch (...) {}
			if (v > 0.0f) {
				font_size = v;
			}
		} else if (key == "font") {
			font = val;
		} else if (key == "code_font") {
			code_font = val;
			explicit_code_font = true;
		} else if (key == "writing_font") {
			writing_font = val;
			explicit_writing_font = true;
		} else if (key == "theme") {
			theme = val;
		} else if (key == "background" || key == "bg") {
			std::string v = val;
			std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
				return (char) std::tolower(c);
			});
			if (v == "light" || v == "dark")
				background = v;
		} else if (key == "syntax") {
			std::string v = val;
			std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
				return (char) std::tolower(c);
			});
			if (v == "1" || v == "on" || v == "true" || v == "yes") {
				syntax = true;
			} else if (v == "0" || v == "off" || v == "false" || v == "no") {
				syntax = false;
			}
		}
	}

	// If code_font was not explicitly set, default it to the main font
	// so that the edit-mode font switcher doesn't immediately switch away
	// from the font loaded during Init.
	if (!explicit_code_font)
		code_font = font;
	if (!explicit_writing_font && writing_font == "crimsonpro" && font != "default")
		writing_font = font;

	return true;
}
