/*
 * RegexEngine.h - the regex engine behind search, replace and match
 * highlighting.
 *
 * With KTE_USE_PCRE2 (the default when libpcre2-8 is found) patterns are
 * compiled by PCRE2 with its JIT: typically one to two orders of magnitude
 * faster than std::regex, no recursion on the C stack, and a match limit
 * that stops catastrophic backtracking (e.g. "^(a+)+$") instead of hanging
 * the editor. Otherwise std::regex (ECMAScript) is used, run on a large
 * stack (see RegexGuard.h). The syntax both accept covers ordinary use;
 * PCRE2 additionally has lookbehind, possessive quantifiers and the like.
 *
 * Replacement strings use ECMAScript syntax with either engine, as
 * std::regex_replace (libstdc++) reads it: $& or $0 (the match), $1..$99
 * (groups, two digits read greedily; a group the pattern lacks expands to
 * nothing), $` (the text since the previous match), $' (the rest of the
 * line), $$ (a dollar sign). As in ECMAScript, '.' matches neither CR nor
 * LF and '$' matches only at the end of the line's text.
 */
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace kte {
class Regex {
public:
	Regex();

	~Regex();

	Regex(Regex &&other) noexcept;

	Regex &operator=(Regex &&other) noexcept;

	Regex(const Regex &) = delete;

	Regex &operator=(const Regex &) = delete;

	// Compile; on failure err describes the problem and Valid() is false.
	bool Compile(std::string_view pattern, std::string &err);

	[[nodiscard]] bool Valid() const;

	// The first match in `subject` starting at byte `from` or later; `from`
	// may be inside the subject (anchors and lookbehind see what precedes
	// it). False if there is none, or if the engine gave up (LimitHit()).
	bool Search(std::string_view subject, std::size_t from, std::size_t &pos, std::size_t &len) const;

	// Every non-overlapping match replaced by `fmt` (ECMAScript syntax, see
	// above); an empty match is not repeated at the same position.
	[[nodiscard]] std::string ReplaceAll(std::string_view subject, std::string_view fmt) const;

	// Whether the last Search/ReplaceAll stopped at the engine's match limit.
	[[nodiscard]] bool LimitHit() const;

	// "PCRE2" or "std::regex".
	static const char *EngineName();

	// The longest line search-as-you-type and match highlighting run the
	// regex over (per keystroke/frame); std::regex can be quadratic in line
	// length with no way to interrupt it, PCRE2 is bounded by its limit.
	static std::size_t IncrementalLineLimit();

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace kte
