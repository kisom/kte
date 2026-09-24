#include "RegexEngine.h"

#include <regex>

#if defined(KTE_USE_PCRE2)
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#endif

namespace kte {
namespace {
// Expand an ECMAScript replacement for one match, as std::regex_replace
// does: $` is the text since the previous match (match_results::prefix()).
// group(i, begin, end) returns whether group i participated and where;
// ngroups counts group 0.
template<typename GroupFn>
void
append_format(std::string &out, std::string_view fmt, std::string_view subject, std::size_t prev_end,
              std::size_t ms, std::size_t me, std::size_t ngroups, GroupFn &&group)
{
	for (std::size_t i = 0; i < fmt.size(); ++i) {
		const char c = fmt[i];
		if (c != '$' || i + 1 >= fmt.size()) {
			out.push_back(c);
			continue;
		}
		const char n = fmt[i + 1];
		if (n == '$') {
			out.push_back('$');
			++i;
		} else if (n == '&') {
			out.append(subject.substr(ms, me - ms));
			++i;
		} else if (n == '`') {
			out.append(subject.substr(prev_end, ms - prev_end));
			++i;
		} else if (n == '\'') {
			out.append(subject.substr(me));
			++i;
		} else if (n >= '0' && n <= '9') {
			// $nn if that group exists, else $n; $0 is not a reference.
			std::size_t idx  = static_cast<std::size_t>(n - '0');
			std::size_t used = 1;
			if (i + 2 < fmt.size() && fmt[i + 2] >= '0' && fmt[i + 2] <= '9') {
				const std::size_t two = idx * 10 + static_cast<std::size_t>(fmt[i + 2] - '0');
				if (two >= 1 && two < ngroups) {
					idx  = two;
					used = 2;
				}
			}
			if (idx == 0 || idx >= ngroups) {
				out.push_back(c); // not a group reference: literal
				continue;
			}
			std::size_t b = 0, e = 0;
			if (group(idx, b, e))
				out.append(subject.substr(b, e - b));
			i += used;
		} else {
			out.push_back(c);
		}
	}
}
} // namespace


#if defined(KTE_USE_PCRE2)

// PCRE2 gives up on a match attempt after this many internal steps
// (catastrophic backtracking), reporting it instead of running for ages.
static constexpr std::uint32_t kMatchLimit = 10000000;


struct Regex::Impl {
	pcre2_code *code               = nullptr;
	pcre2_match_data *md           = nullptr;
	pcre2_match_context *mctx      = nullptr;
	std::size_t ngroups            = 1; // including group 0
	mutable bool limit_hit         = false;


	~Impl()
	{
		if (md)
			pcre2_match_data_free(md);
		if (mctx)
			pcre2_match_context_free(mctx);
		if (code)
			pcre2_code_free(code);
	}


	// pcre2_match with a subject that is never a null pointer.
	int match(std::string_view s, std::size_t from, std::uint32_t opts) const
	{
		static const char empty = '\0';
		const char *p           = s.data() ? s.data() : &empty;
		const int rc = pcre2_match(code, reinterpret_cast<PCRE2_SPTR>(p), s.size(), from, opts, md, mctx);
		if (rc == PCRE2_ERROR_MATCHLIMIT || rc == PCRE2_ERROR_DEPTHLIMIT || rc == PCRE2_ERROR_HEAPLIMIT)
			limit_hit = true;
		return rc;
	}
};


Regex::Regex() = default;

Regex::~Regex() = default;

Regex::Regex(Regex &&other) noexcept = default;

Regex &Regex::operator=(Regex &&other) noexcept = default;


bool
Regex::Compile(std::string_view pattern, std::string &err)
{
	err.clear();
	impl_.reset();
	auto impl = std::make_unique<Impl>();
	int errcode        = 0;
	PCRE2_SIZE erroff  = 0;
	static const char empty = '\0';
	impl->code = pcre2_compile(reinterpret_cast<PCRE2_SPTR>(pattern.data() ? pattern.data() : &empty),
	                           pattern.size(), 0, &errcode, &erroff, nullptr);
	if (!impl->code) {
		PCRE2_UCHAR msg[256];
		pcre2_get_error_message(errcode, msg, sizeof(msg));
		err = std::string(reinterpret_cast<const char *>(msg)) + " at offset " + std::to_string(erroff);
		return false;
	}
	(void) pcre2_jit_compile(impl->code, PCRE2_JIT_COMPLETE); // interpreted if unavailable
	impl->md   = pcre2_match_data_create_from_pattern(impl->code, nullptr);
	impl->mctx = pcre2_match_context_create(nullptr);
	if (!impl->md || !impl->mctx) {
		err = "out of memory compiling the pattern";
		return false;
	}
	pcre2_set_match_limit(impl->mctx, kMatchLimit);
	std::uint32_t captures = 0;
	(void) pcre2_pattern_info(impl->code, PCRE2_INFO_CAPTURECOUNT, &captures);
	impl->ngroups = static_cast<std::size_t>(captures) + 1;
	impl_         = std::move(impl);
	return true;
}


bool
Regex::Search(std::string_view subject, std::size_t from, std::size_t &pos, std::size_t &len) const
{
	if (!impl_ || from > subject.size())
		return false;
	impl_->limit_hit = false;
	if (impl_->match(subject, from, 0) <= 0)
		return false;
	const PCRE2_SIZE *ov = pcre2_get_ovector_pointer(impl_->md);
	pos                  = ov[0];
	len                  = ov[1] - ov[0];
	return true;
}


std::string
Regex::ReplaceAll(std::string_view s, std::string_view fmt) const
{
	if (!impl_)
		return std::string(s);
	impl_->limit_hit = false;
	std::string out;
	std::size_t copied = 0; // subject bytes up to here are in `out`
	std::size_t start  = 0;
	std::uint32_t opts = 0;
	while (start <= s.size()) {
		const int rc = impl_->match(s, start, opts);
		if (rc == PCRE2_ERROR_NOMATCH && opts != 0) {
			// No non-empty match right after an empty one: step past a byte.
			if (start >= s.size())
				break;
			++start;
			opts = 0;
			continue;
		}
		if (rc <= 0) {
			if (impl_->limit_hit)
				return std::string(s); // gave up: leave the text as it was
			break;
		}
		const PCRE2_SIZE *ov = pcre2_get_ovector_pointer(impl_->md);
		const std::size_t ms = ov[0], me = ov[1];
		out.append(s.substr(copied, ms - copied));
		append_format(out, fmt, s, copied, ms, me, impl_->ngroups, [&](std::size_t i, std::size_t &b, std::size_t &e) {
			if (i >= static_cast<std::size_t>(rc) || ov[2 * i] == PCRE2_UNSET)
				return false;
			b = ov[2 * i];
			e = ov[2 * i + 1];
			return true;
		});
		copied = me;
		start  = me;
		// After an empty match, the next one must not be empty at the same place.
		opts = (me == ms) ? (PCRE2_NOTEMPTY_ATSTART | PCRE2_ANCHORED) : 0;
	}
	out.append(s.substr(copied));
	return out;
}


bool
Regex::Valid() const
{
	return static_cast<bool>(impl_);
}


bool
Regex::LimitHit() const
{
	return impl_ && impl_->limit_hit;
}


const char *
Regex::EngineName()
{
	return "PCRE2";
}


std::size_t
Regex::IncrementalLineLimit()
{
	return std::size_t{1} << 20;
}

#else // std::regex

struct Regex::Impl {
	std::regex rx;
};


Regex::Regex() = default;

Regex::~Regex() = default;

Regex::Regex(Regex &&other) noexcept = default;

Regex &Regex::operator=(Regex &&other) noexcept = default;


bool
Regex::Compile(std::string_view pattern, std::string &err)
{
	err.clear();
	impl_.reset();
	try {
		auto impl = std::make_unique<Impl>();
		impl->rx  = std::regex(pattern.begin(), pattern.end());
		impl_     = std::move(impl);
		return true;
	} catch (const std::regex_error &e) {
		err = e.what();
		return false;
	}
}


bool
Regex::Search(std::string_view subject, std::size_t from, std::size_t &pos, std::size_t &len) const
{
	if (!impl_ || from > subject.size())
		return false;
	std::cmatch m;
	const auto flags = from > 0 ? std::regex_constants::match_prev_avail : std::regex_constants::match_default;
	const char *b    = subject.data() + from;
	const char *e    = subject.data() + subject.size();
	if (!std::regex_search(b, e, m, impl_->rx, flags))
		return false;
	pos = from + static_cast<std::size_t>(m.position(0));
	len = static_cast<std::size_t>(m.length(0));
	return true;
}


std::string
Regex::ReplaceAll(std::string_view s, std::string_view fmt) const
{
	if (!impl_)
		return std::string(s);
	std::string out;
	std::regex_replace(std::back_inserter(out), s.begin(), s.end(), impl_->rx, std::string(fmt));
	return out;
}


bool
Regex::Valid() const
{
	return static_cast<bool>(impl_);
}


bool
Regex::LimitHit() const
{
	return false;
}


const char *
Regex::EngineName()
{
	return "std::regex";
}


std::size_t
Regex::IncrementalLineLimit()
{
	return 2000;
}

#endif
} // namespace kte
