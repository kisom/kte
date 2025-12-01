/*
 * HelpText.h - embedded/customizable help content
 */
#ifndef KTE_HELPTEXT_H
#define KTE_HELPTEXT_H

#include <string>

class HelpText {
public:
	// Returns the embedded help text as a single string with newlines.
	// Project maintainers can customize the returned string below
	// (in HelpText.cc) without touching the help command logic.
	static std::string Text();
};

#endif // KTE_HELPTEXT_H
