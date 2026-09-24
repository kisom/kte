// RegexGuard.h - keep std::regex from overflowing the stack
//
// libstdc++'s std::regex matcher recurses roughly once per character of a
// match, so a pattern such as ".*" or "a+" on a line of a few tens of
// thousands of characters (minified JS/JSON) overflows a default 8 MiB
// stack and crashes the editor.
#pragma once

#include <cstddef>
#include <functional>

namespace kte {
// Renderers highlight regex matches only on lines up to this many bytes;
// longer lines are drawn without match highlighting.
constexpr std::size_t kRegexRenderLineLimit = 10000;

// Run fn to completion on a thread with a large stack (address space is
// reserved, pages are committed only as used), so regex work over long lines
// cannot overflow. If such a thread cannot be created, fn runs on the calling
// thread. Exceptions thrown by fn are rethrown to the caller.
void RunWithLargeStack(const std::function<void()> &fn);
} // namespace kte
