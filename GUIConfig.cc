#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>

#include "GUIConfig.h"


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
default_config_path()
{
	const char *home = std::getenv("HOME");
	if (!home || !*home)
		return {};
	std::string path(home);
	path += "/.config/kte/kge.ini";
	return path;
}


GUIConfig
GUIConfig::Load()
{
	GUIConfig cfg; // defaults already set
	const std::string path = default_config_path();

	if (!path.empty()) {
		cfg.LoadFromFile(path);
	}
	return cfg;
}


bool
GUIConfig::LoadFromFile(const std::string &path)
{
	std::ifstream in(path);
	if (!in.good())
		return false;

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

    return true;
}
