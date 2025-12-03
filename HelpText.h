/*
 * HelpText.h - embedded/customizable help content
 */
#pragma once
#include <string>

class HelpText {
public:
	// Returns the embedded help text as a single string with newlines.
	// Project maintainers can customize the returned string below
	// (in HelpText.cc) without touching the help command logic.
	static std::string Text();
};