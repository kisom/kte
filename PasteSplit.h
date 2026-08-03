/*
 * PasteSplit.h - split pasted/clipboard text into editor commands
 *
 * Pure logic (no SDL/GUI dependencies) so it can be unit tested and shared
 * across frontends. Line breaks in the pasted text must become Newline
 * commands; InsertText itself rejects embedded '\r'/'\n' (see Command.cc).
 */
#pragma once
#include <string>
#include <vector>

#include "InputHandler.h"


// Translate a block of pasted text into a sequence of editor commands.
//
// Any of "\n", "\r\n", or a bare "\r" is treated as a single line break and
// emitted as a Newline command; the text between breaks becomes InsertText
// commands. Empty text segments are skipped, but line breaks are always
// emitted so blank lines round-trip correctly.
inline std::vector<MappedInput> SplitPasteIntoCommands(const std::string &text)
{
	std::vector<MappedInput> out;
	std::string segment;

	auto flush_segment = [&]() {
		if (!segment.empty()) {
			out.push_back(MappedInput{true, CommandId::InsertText, segment, 0});
			segment.clear();
		}
	};

	for (std::size_t i = 0; i < text.size(); ++i) {
		const char c = text[i];
		if (c == '\n' || c == '\r') {
			flush_segment();
			out.push_back(MappedInput{true, CommandId::Newline, std::string(), 0});
			// Collapse a "\r\n" pair into one line break.
			if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
				++i;
		} else {
			segment.push_back(c);
		}
	}
	flush_segment();

	return out;
}
