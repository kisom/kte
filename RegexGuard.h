// RegexGuard.h - keep std::regex from overflowing the stack
//
// libstdc++'s std::regex matcher recurses roughly once per character of a
// match, so a pattern such as ".*" or "a+" on a line of a few tens of
// thousands of characters (minified JS/JSON) overflows a default 8 MiB
// stack and crashes the editor.
#pragma once

#include <cstddef>
#include <functional>
#include <regex>
#include <string>

namespace kte {
// Used where std::regex is still called directly (the Qt frontend's match
// highlighting; search and the other renderers use RegexEngine.h, whose
// Regex::IncrementalLineLimit() applies the same limits for std::regex).
// Regex matches are highlighted only on lines up to this many bytes:
// std::regex can take time quadratic in line length even for ordinary
// patterns (".*q" on a 20 KB line: 12 s), with no way to interrupt it, and
// these run on every frame or keystroke.
constexpr std::size_t kRegexRenderLineLimit = 2000;

// Run fn to completion on a thread with a large stack (address space is
// reserved, pages are committed only as used), so regex work over long lines
// cannot overflow. If such a thread cannot be created, fn runs on the calling
// thread. Exceptions thrown by fn are rethrown to the caller.
void RunWithLargeStack(const std::function<void()> &fn);

// Call on_match(position, length) for each match of rx in line. Lines short
// enough to be safe on any stack are matched inline; longer ones on a large
// stack (a 10,000-byte line already overflowed 8 MiB for some patterns).
// Throws what std::regex throws.
void ForEachRegexMatch(const std::string &line, const std::regex &rx,
                       const std::function<void(std::size_t, std::size_t)> &on_match);
} // namespace kte
